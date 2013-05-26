#include <assert.h>
#include <future>
#include <vector>

#include "BlockData.hpp"
#include "ColorSpace.hpp"
#include "Debug.hpp"

static v3b Average( const uint8* data )
{
    uint32 r = 0, g = 0, b = 0;
    for( int i=0; i<8; i++ )
    {
        b += *data++;
        g += *data++;
        r += *data++;
    }
    return v3b( r / 8, g / 8, b / 8 );
}

static float CalcError( Color::Lab* c, v3b average )
{
    float err = 0;
    Color::Lab avg( average );
    for( int i=0; i<8; i++ )
    {
        err += sq( c[i].L - avg.L ) + sq( c[i].a - avg.a ) + sq( c[i].b - avg.b );
    }
    return err;
}

static float CalcError( const uint8* data, v3b average )
{
    float err = 0;
    for( int i=0; i<8; i++ )
    {
        uint32 b = *data++;
        uint32 g = *data++;
        uint32 r = *data++;
        err += sq( r - average.x ) + sq( g - average.y ) + sq( b - average.z );
    }
    return err;
}

static inline Color::Lab ToLab( const uint8* data )
{
    uint32 b = *data++;
    uint32 g = *data++;
    uint32 r = *data;
    return Color::Lab( v3b( r, g, b ) );
}

BlockData::BlockData( const BlockBitmapPtr& bitmap, bool perc )
    : m_size( bitmap->Size() )
    , m_perc( perc )
{
    assert( m_size.x%4 == 0 && m_size.y%4 == 0 );

    uint32 cnt = m_size.x * m_size.y / 16;
    DBGPRINT( cnt << " blocks" );
    m_data = new uint64[cnt];

    const uint8* src = bitmap->Data();
    uint64* dst = m_data;

    std::vector<std::future<void>> vec;
    uint32 step = cnt / 16;
    for( uint32 i=0; i<cnt; i+=step )
    {
        vec.push_back( std::async( std::launch::async, [src, dst, step, cnt, i, this]{ ProcessBlocks( src, dst, std::min( step, cnt - i ) ); } ) );

        src += 4*4*3 * step;
        dst += step;
    }
    for( auto& f : vec )
    {
        f.wait();
    }
}

BlockData::~BlockData()
{
    delete[] m_data;
}

BitmapPtr BlockData::Decode()
{
    auto ret = std::make_shared<Bitmap>( m_size );

    static const int32 table[][4] = {
        {  2,  8,   -2,   -8 },
        {  5, 17,   -5,  -17 },
        {  9, 29,   -9,  -29 },
        { 13, 42,  -13,  -42 },
        { 18, 60,  -18,  -60 },
        { 24, 80,  -24,  -80 },
        { 33, 106, -33, -106 },
        { 47, 183, -47, -183 } };

    uint32* l[4];
    l[0] = ret->Data();
    l[1] = l[0] + m_size.x;
    l[2] = l[1] + m_size.x;
    l[3] = l[2] + m_size.x;

    const uint64* src = (const uint64*)m_data;

    for( int y=0; y<m_size.y/4; y++ )
    {
        for( int x=0; x<m_size.x/4; x++ )
        {
            uint64 d = *src++;

            uint32 r1, g1, b1;
            uint32 r2, g2, b2;

            if( d & 0x2 )
            {
                int32 dr, dg, db;

                r1 = ( d & 0xF8000000 ) >> 27;
                g1 = ( d & 0x00F80000 ) >> 19;
                b1 = ( d & 0x0000F800 ) >> 11;

                dr = ( d & 0x07000000 ) >> 24;
                dg = ( d & 0x00070000 ) >> 16;
                db = ( d & 0x00000700 ) >> 8;

                if( dr & 0x4 )
                {
                    dr |= 0xFFFFFFF8;
                }
                if( dg & 0x4 )
                {
                    dg |= 0xFFFFFFF8;
                }
                if( db & 0x4 )
                {
                    db |= 0xFFFFFFF8;
                }

                r2 = r1 + dr;
                g2 = g1 + dg;
                b2 = b1 + db;

                r1 = ( r1 << 3 ) | ( r1 >> 2 );
                g1 = ( g1 << 3 ) | ( g1 >> 2 );
                b1 = ( b1 << 3 ) | ( b1 >> 2 );
                r2 = ( r2 << 3 ) | ( r2 >> 2 );
                g2 = ( g2 << 3 ) | ( g2 >> 2 );
                b2 = ( b2 << 3 ) | ( b2 >> 2 );
            }
            else
            {
                r1 = ( ( d & 0xF0000000 ) >> 24 ) | ( ( d & 0xF0000000 ) >> 28 );
                r2 = ( ( d & 0x0F000000 ) >> 20 ) | ( ( d & 0x0F000000 ) >> 24 );
                g1 = ( ( d & 0x00F00000 ) >> 16 ) | ( ( d & 0x00F00000 ) >> 20 );
                g2 = ( ( d & 0x000F0000 ) >> 12 ) | ( ( d & 0x000F0000 ) >> 16 );
                b1 = ( ( d & 0x0000F000 ) >> 8  ) | ( ( d & 0x0000F000 ) >> 12 );
                b2 = ( ( d & 0x00000F00 ) >> 4  ) | ( ( d & 0x00000F00 ) >> 8  );
            }

            uint tcw[2];
            tcw[0] = ( d & 0xE0 ) >> 5;
            tcw[1] = ( d & 0x1C ) >> 2;

            uint ra, ga, ba;
            uint rb, gb, bb;
            uint rc, gc, bc;
            uint rd, gd, bd;

            if( d & 0x1 )
            {
                int o = 0;
                for( int i=0; i<4; i++ )
                {
                    ra = clampu8( r1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 32 ) ) ) >> ( o + 32 ) ) | ( ( d & ( 1ll << ( o + 48 ) ) ) >> ( o + 47 ) ) ] );
                    ga = clampu8( g1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 32 ) ) ) >> ( o + 32 ) ) | ( ( d & ( 1ll << ( o + 48 ) ) ) >> ( o + 47 ) ) ] );
                    ba = clampu8( b1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 32 ) ) ) >> ( o + 32 ) ) | ( ( d & ( 1ll << ( o + 48 ) ) ) >> ( o + 47 ) ) ] );

                    rb = clampu8( r1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 33 ) ) ) >> ( o + 33 ) ) | ( ( d & ( 1ll << ( o + 49 ) ) ) >> ( o + 48 ) ) ] );
                    gb = clampu8( g1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 33 ) ) ) >> ( o + 33 ) ) | ( ( d & ( 1ll << ( o + 49 ) ) ) >> ( o + 48 ) ) ] );
                    bb = clampu8( b1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 33 ) ) ) >> ( o + 33 ) ) | ( ( d & ( 1ll << ( o + 49 ) ) ) >> ( o + 48 ) ) ] );

                    rc = clampu8( r2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 34 ) ) ) >> ( o + 34 ) ) | ( ( d & ( 1ll << ( o + 50 ) ) ) >> ( o + 49 ) ) ] );
                    gc = clampu8( g2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 34 ) ) ) >> ( o + 34 ) ) | ( ( d & ( 1ll << ( o + 50 ) ) ) >> ( o + 49 ) ) ] );
                    bc = clampu8( b2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 34 ) ) ) >> ( o + 34 ) ) | ( ( d & ( 1ll << ( o + 50 ) ) ) >> ( o + 49 ) ) ] );

                    rd = clampu8( r2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 35 ) ) ) >> ( o + 35 ) ) | ( ( d & ( 1ll << ( o + 51 ) ) ) >> ( o + 50 ) ) ] );
                    gd = clampu8( g2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 35 ) ) ) >> ( o + 35 ) ) | ( ( d & ( 1ll << ( o + 51 ) ) ) >> ( o + 50 ) ) ] );
                    bd = clampu8( b2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 35 ) ) ) >> ( o + 35 ) ) | ( ( d & ( 1ll << ( o + 51 ) ) ) >> ( o + 50 ) ) ] );

                    *l[0]++ = ra | ( ga << 8 ) | ( ba << 16 ) | 0xFF000000;
                    *l[1]++ = rb | ( gb << 8 ) | ( bb << 16 ) | 0xFF000000;
                    *l[2]++ = rc | ( gc << 8 ) | ( bc << 16 ) | 0xFF000000;
                    *l[3]++ = rd | ( gd << 8 ) | ( bd << 16 ) | 0xFF000000;

                    o += 4;
                }
            }
            else
            {
                int o = 0;
                for( int i=0; i<2; i++ )
                {
                    ra = clampu8( r1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 32 ) ) ) >> ( o + 32 ) ) | ( ( d & ( 1ll << ( o + 48 ) ) ) >> ( o + 47 ) ) ] );
                    ga = clampu8( g1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 32 ) ) ) >> ( o + 32 ) ) | ( ( d & ( 1ll << ( o + 48 ) ) ) >> ( o + 47 ) ) ] );
                    ba = clampu8( b1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 32 ) ) ) >> ( o + 32 ) ) | ( ( d & ( 1ll << ( o + 48 ) ) ) >> ( o + 47 ) ) ] );

                    rb = clampu8( r1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 33 ) ) ) >> ( o + 33 ) ) | ( ( d & ( 1ll << ( o + 49 ) ) ) >> ( o + 48 ) ) ] );
                    gb = clampu8( g1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 33 ) ) ) >> ( o + 33 ) ) | ( ( d & ( 1ll << ( o + 49 ) ) ) >> ( o + 48 ) ) ] );
                    bb = clampu8( b1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 33 ) ) ) >> ( o + 33 ) ) | ( ( d & ( 1ll << ( o + 49 ) ) ) >> ( o + 48 ) ) ] );

                    rc = clampu8( r1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 34 ) ) ) >> ( o + 34 ) ) | ( ( d & ( 1ll << ( o + 50 ) ) ) >> ( o + 49 ) ) ] );
                    gc = clampu8( g1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 34 ) ) ) >> ( o + 34 ) ) | ( ( d & ( 1ll << ( o + 50 ) ) ) >> ( o + 49 ) ) ] );
                    bc = clampu8( b1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 34 ) ) ) >> ( o + 34 ) ) | ( ( d & ( 1ll << ( o + 50 ) ) ) >> ( o + 49 ) ) ] );

                    rd = clampu8( r1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 35 ) ) ) >> ( o + 35 ) ) | ( ( d & ( 1ll << ( o + 51 ) ) ) >> ( o + 50 ) ) ] );
                    gd = clampu8( g1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 35 ) ) ) >> ( o + 35 ) ) | ( ( d & ( 1ll << ( o + 51 ) ) ) >> ( o + 50 ) ) ] );
                    bd = clampu8( b1 + table[tcw[0]][ ( ( d & ( 1ll << ( o + 35 ) ) ) >> ( o + 35 ) ) | ( ( d & ( 1ll << ( o + 51 ) ) ) >> ( o + 50 ) ) ] );

                    *l[0]++ = ra | ( ga << 8 ) | ( ba << 16 ) | 0xFF000000;
                    *l[1]++ = rb | ( gb << 8 ) | ( bb << 16 ) | 0xFF000000;
                    *l[2]++ = rc | ( gc << 8 ) | ( bc << 16 ) | 0xFF000000;
                    *l[3]++ = rd | ( gd << 8 ) | ( bd << 16 ) | 0xFF000000;

                    o += 4;
                }
                for( int i=0; i<2; i++ )
                {
                    ra = clampu8( r2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 32 ) ) ) >> ( o + 32 ) ) | ( ( d & ( 1ll << ( o + 48 ) ) ) >> ( o + 47 ) ) ] );
                    ga = clampu8( g2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 32 ) ) ) >> ( o + 32 ) ) | ( ( d & ( 1ll << ( o + 48 ) ) ) >> ( o + 47 ) ) ] );
                    ba = clampu8( b2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 32 ) ) ) >> ( o + 32 ) ) | ( ( d & ( 1ll << ( o + 48 ) ) ) >> ( o + 47 ) ) ] );

                    rb = clampu8( r2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 33 ) ) ) >> ( o + 33 ) ) | ( ( d & ( 1ll << ( o + 49 ) ) ) >> ( o + 48 ) ) ] );
                    gb = clampu8( g2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 33 ) ) ) >> ( o + 33 ) ) | ( ( d & ( 1ll << ( o + 49 ) ) ) >> ( o + 48 ) ) ] );
                    bb = clampu8( b2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 33 ) ) ) >> ( o + 33 ) ) | ( ( d & ( 1ll << ( o + 49 ) ) ) >> ( o + 48 ) ) ] );

                    rc = clampu8( r2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 34 ) ) ) >> ( o + 34 ) ) | ( ( d & ( 1ll << ( o + 50 ) ) ) >> ( o + 49 ) ) ] );
                    gc = clampu8( g2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 34 ) ) ) >> ( o + 34 ) ) | ( ( d & ( 1ll << ( o + 50 ) ) ) >> ( o + 49 ) ) ] );
                    bc = clampu8( b2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 34 ) ) ) >> ( o + 34 ) ) | ( ( d & ( 1ll << ( o + 50 ) ) ) >> ( o + 49 ) ) ] );

                    rd = clampu8( r2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 35 ) ) ) >> ( o + 35 ) ) | ( ( d & ( 1ll << ( o + 51 ) ) ) >> ( o + 50 ) ) ] );
                    gd = clampu8( g2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 35 ) ) ) >> ( o + 35 ) ) | ( ( d & ( 1ll << ( o + 51 ) ) ) >> ( o + 50 ) ) ] );
                    bd = clampu8( b2 + table[tcw[1]][ ( ( d & ( 1ll << ( o + 35 ) ) ) >> ( o + 35 ) ) | ( ( d & ( 1ll << ( o + 51 ) ) ) >> ( o + 50 ) ) ] );

                    *l[0]++ = ra | ( ga << 8 ) | ( ba << 16 ) | 0xFF000000;
                    *l[1]++ = rb | ( gb << 8 ) | ( bb << 16 ) | 0xFF000000;
                    *l[2]++ = rc | ( gc << 8 ) | ( bc << 16 ) | 0xFF000000;
                    *l[3]++ = rd | ( gd << 8 ) | ( bd << 16 ) | 0xFF000000;

                    o += 4;
                }
            }
        }

        l[0] += m_size.x * 3;
        l[1] += m_size.x * 3;
        l[2] += m_size.x * 3;
        l[3] += m_size.x * 3;
    }

    return ret;
}

void BlockData::ProcessBlocks( const uint8* src, uint64* dst, uint num )
{
    do
    {
        uint64 d = 0;

        uint8 b[4][24];

        memcpy( b[1], src, 24 );
        memcpy( b[0], src+24, 24 );

        for( int i=0; i<4; i++ )
        {
            memcpy( b[3]+i*6, src+i*12, 6 );
            memcpy( b[2]+i*6, src+i*12+6, 6 );
        }

        v3b a[8];
        for( int i=0; i<4; i++ )
        {
            a[i] = Average( b[i] );
        }
        for( int i=0; i<2; i++ )
        {
            for( int j=0; j<3; j++ )
            {
                int32 c1 = a[i*2][j] >> 3;
                int32 c2 = c1 - ( a[i*2+1][j] >> 3 );
                c2 = std::min( std::max( -4, c2 ), 3 );
                a[4+i*2][j] = c1 << 3;
                a[5+i*2][j] = ( c1 + c2 ) << 3;
            }
        }
        for( int i=0; i<4; i++ )
        {
            for( int j=0; j<3; j++ )
            {
                a[i][j] &= 0xF0;
            }
        }

        float err[4] = { 0, 0, 0, 0 };

        if( m_perc )
        {
            Color::Lab lab[4][8];
            {
                Color::Lab tmp[16];
                for( int i=0; i<16; i++ )
                {
                    tmp[i] = ToLab( src + i*3 );
                }
                for( int i=0; i<8; i++ )
                {
                    lab[1][i] = tmp[i];
                    lab[0][i] = tmp[i+8];
                }
                for( int i=0; i<4; i++ )
                {
                    lab[3][i*2] = tmp[i*4];
                    lab[3][i*2+1] = tmp[i*4+1];
                    lab[2][i*2] = tmp[i*4+2];
                    lab[2][i*2+1] = tmp[i*4+3];
                }
            }

            for( int i=0; i<4; i++ )
            {
                err[i/2] += CalcError( lab[i], a[i] );
                err[2+i/2] += CalcError( lab[i], a[i+4] );
            }
        }
        else
        {
            for( int i=0; i<4; i++ )
            {
                err[i/2] += CalcError( b[i], a[i] );
                err[2+i/2] += CalcError( b[i], a[i+4] );
            }
        }

        int idx = 0;
        for( int i=1; i<4; i++ )
        {
            if( err[i] < err[idx] )
            {
                idx = i;
            }
        }

        d |= idx ^ 0x1;
        int base = ( idx & 0x1 ) != 0 ? 2 : 0;

        if( ( idx & 0x2 ) == 0 )
        {
            for( int i=0; i<3; i++ )
            {
                d |= a[base+0][i] << ( i*8 + 4 );
                d |= a[base+1][i] << ( i*8 + 8 );
            }
        }
        else
        {
            for( int i=0; i<3; i++ )
            {
                d |= a[base+4][i] << ( i*8 + 8 );
                int8 c = ( a[base+5][i] - a[base+4][i] ) >> 3;
                c &= ~0xF8;
                d |= ((uint32)c) << ( i*8 + 8 );
            }
        }

        src += 4*4*3;
        *dst++ = d;
    }
    while( --num );
}