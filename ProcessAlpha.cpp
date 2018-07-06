#include <limits>

#include "Math.hpp"
#include "ProcessAlpha.hpp"
#include "Tables.hpp"

#ifdef __SSE4_1__
#  ifdef _MSC_VER
#    include <intrin.h>
#    include <Windows.h>
#    define _bswap(x) _byteswap_ulong(x)
#    define _bswap64(x) _byteswap_uint64(x)
#  else
#    include <x86intrin.h>
#  endif
#else
#  ifndef _MSC_VER
#    include <byteswap.h>
#    define _bswap(x) bswap_32(x)
#    define _bswap64(x) bswap_64(x)
#  endif
#endif

#ifndef _bswap
#  define _bswap(x) __builtin_bswap32(x)
#  define _bswap64(x) __builtin_bswap64(x)
#endif

#ifdef __SSE4_1__
template<int K>
static inline __m128i Widen( const __m128i src )
{
    static_assert( K >= 0 && K <= 7, "Index out of range" );

    __m128i tmp;
    switch( K )
    {
    case 0:
        tmp = _mm_shufflelo_epi16( src, _MM_SHUFFLE( 0, 0, 0, 0 ) );
        return _mm_shuffle_epi32( tmp, _MM_SHUFFLE( 0, 0, 0, 0 ) );
    case 1:
        tmp = _mm_shufflelo_epi16( src, _MM_SHUFFLE( 1, 1, 1, 1 ) );
        return _mm_shuffle_epi32( tmp, _MM_SHUFFLE( 0, 0, 0, 0 ) );
    case 2:
        tmp = _mm_shufflelo_epi16( src, _MM_SHUFFLE( 2, 2, 2, 2 ) );
        return _mm_shuffle_epi32( tmp, _MM_SHUFFLE( 0, 0, 0, 0 ) );
    case 3:
        tmp = _mm_shufflelo_epi16( src, _MM_SHUFFLE( 3, 3, 3, 3 ) );
        return _mm_shuffle_epi32( tmp, _MM_SHUFFLE( 0, 0, 0, 0 ) );
    case 4:
        tmp = _mm_shufflehi_epi16( src, _MM_SHUFFLE( 0, 0, 0, 0 ) );
        return _mm_shuffle_epi32( tmp, _MM_SHUFFLE( 2, 2, 2, 2 ) );
    case 5:
        tmp = _mm_shufflehi_epi16( src, _MM_SHUFFLE( 1, 1, 1, 1 ) );
        return _mm_shuffle_epi32( tmp, _MM_SHUFFLE( 2, 2, 2, 2 ) );
    case 6:
        tmp = _mm_shufflehi_epi16( src, _MM_SHUFFLE( 2, 2, 2, 2 ) );
        return _mm_shuffle_epi32( tmp, _MM_SHUFFLE( 2, 2, 2, 2 ) );
    case 7:
        tmp = _mm_shufflehi_epi16( src, _MM_SHUFFLE( 3, 3, 3, 3 ) );
        return _mm_shuffle_epi32( tmp, _MM_SHUFFLE( 2, 2, 2, 2 ) );
    }
}

static inline int GetMulSel( int sel )
{
    switch( sel )
    {
    case 0:
        return 0;
    case 1:
    case 2:
    case 3:
        return 1;
    case 4:
        return 2;
    case 5:
    case 6:
    case 7:
        return 3;
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
        return 4;
    case 14:
    case 15:
        return 5;
    }
}
#endif

uint64_t ProcessAlpha( const uint8_t* src )
{
#if defined __SSE4_1__
    // Check solid
    __m128i s = _mm_loadu_si128( (__m128i*)src );
    __m128i solidCmp = _mm_set1_epi8( src[0] );
    __m128i cmpRes = _mm_cmpeq_epi8( s, solidCmp );
    if( _mm_testc_si128( cmpRes, _mm_set1_epi32( -1 ) ) )
    {
        return src[0];
    }

    // Calculate min, max
    __m128i s1 = _mm_shuffle_epi32( s, _MM_SHUFFLE( 2, 3, 0, 1 ) );
    __m128i max1 = _mm_max_epu8( s, s1 );
    __m128i min1 = _mm_min_epu8( s, s1 );
    __m128i smax2 = _mm_shuffle_epi32( max1, _MM_SHUFFLE( 0, 0, 2, 2 ) );
    __m128i smin2 = _mm_shuffle_epi32( min1, _MM_SHUFFLE( 0, 0, 2, 2 ) );
    __m128i max2 = _mm_max_epu8( max1, smax2 );
    __m128i min2 = _mm_min_epu8( min1, smin2 );
    __m128i smax3 = _mm_alignr_epi8( max2, max2, 2 );
    __m128i smin3 = _mm_alignr_epi8( min2, min2, 2 );
    __m128i max3 = _mm_max_epu8( max2, smax3 );
    __m128i min3 = _mm_min_epu8( min2, smin3 );
    __m128i smax4 = _mm_alignr_epi8( max3, max3, 1 );
    __m128i smin4 = _mm_alignr_epi8( min3, min3, 1 );
    __m128i max = _mm_max_epu8( max3, smax4 );
    __m128i min = _mm_min_epu8( min3, smin4 );
    __m128i max16 = _mm_unpacklo_epi8( max, _mm_setzero_si128() );
    __m128i min16 = _mm_unpacklo_epi8( min, _mm_setzero_si128() );

    // src range, mid
    __m128i srcRange = _mm_sub_epi16( max16, min16 );
    __m128i srcRangeHalf = _mm_srli_epi16( srcRange, 1 );
    __m128i srcMid = _mm_add_epi16( min16, srcRangeHalf );

    // multiplier
    __m128i mul1 = _mm_mulhi_epi16( srcRange, g_alphaRange_SIMD );
    __m128i mul = _mm_add_epi16( mul1, _mm_set1_epi16( 1 ) );

    // wide multiplier
    __m128i rangeMul[16] = {
        _mm_mullo_epi16( Widen<0>( mul ), g_alpha_SIMD[0] ),
        _mm_mullo_epi16( Widen<1>( mul ), g_alpha_SIMD[1] ),
        _mm_mullo_epi16( Widen<1>( mul ), g_alpha_SIMD[2] ),
        _mm_mullo_epi16( Widen<1>( mul ), g_alpha_SIMD[3] ),
        _mm_mullo_epi16( Widen<2>( mul ), g_alpha_SIMD[4] ),
        _mm_mullo_epi16( Widen<3>( mul ), g_alpha_SIMD[5] ),
        _mm_mullo_epi16( Widen<3>( mul ), g_alpha_SIMD[6] ),
        _mm_mullo_epi16( Widen<3>( mul ), g_alpha_SIMD[7] ),
        _mm_mullo_epi16( Widen<4>( mul ), g_alpha_SIMD[8] ),
        _mm_mullo_epi16( Widen<4>( mul ), g_alpha_SIMD[9] ),
        _mm_mullo_epi16( Widen<4>( mul ), g_alpha_SIMD[10] ),
        _mm_mullo_epi16( Widen<4>( mul ), g_alpha_SIMD[11] ),
        _mm_mullo_epi16( Widen<4>( mul ), g_alpha_SIMD[12] ),
        _mm_mullo_epi16( Widen<4>( mul ), g_alpha_SIMD[13] ),
        _mm_mullo_epi16( Widen<5>( mul ), g_alpha_SIMD[14] ),
        _mm_mullo_epi16( Widen<5>( mul ), g_alpha_SIMD[15] )
    };

    // wide source
    __m128i s16_1 = _mm_shuffle_epi32( s, _MM_SHUFFLE( 3, 2, 3, 2 ) );
    __m128i s16[2] = { _mm_unpacklo_epi8( s, _mm_setzero_si128() ), _mm_unpacklo_epi8( s16_1, _mm_setzero_si128() ) };

    __m128i sr[16] = {
        Widen<0>( s16[0] ),
        Widen<1>( s16[0] ),
        Widen<2>( s16[0] ),
        Widen<3>( s16[0] ),
        Widen<4>( s16[0] ),
        Widen<5>( s16[0] ),
        Widen<6>( s16[0] ),
        Widen<7>( s16[0] ),
        Widen<0>( s16[1] ),
        Widen<1>( s16[1] ),
        Widen<2>( s16[1] ),
        Widen<3>( s16[1] ),
        Widen<4>( s16[1] ),
        Widen<5>( s16[1] ),
        Widen<6>( s16[1] ),
        Widen<7>( s16[1] )
    };

    // find indices
    int buf[16][16];
    int err = std::numeric_limits<int>::max();
    int sel;
    for( int r=0; r<16; r++ )
    {
        __m128i recVal1 = _mm_add_epi16( srcMid, rangeMul[r] );
        __m128i recVal = _mm_packus_epi16( recVal1, recVal1 );
        __m128i recVal16 = _mm_unpacklo_epi8( recVal, _mm_setzero_si128() );

        int rangeErr = 0;
        for( int i=0; i<16; i++ )
        {
            __m128i err1 = _mm_sub_epi16( sr[i], recVal16 );
            __m128i err = _mm_mullo_epi16( err1, err1 );
            __m128i minerr = _mm_minpos_epu16( err );
            uint64_t tmp = _mm_cvtsi128_si64( minerr );
            buf[r][i] = tmp >> 16;
            rangeErr += tmp & 0xFFFF;
        }

        if( rangeErr < err )
        {
            err = rangeErr;
            sel = r;
            if( err == 0 ) break;
        }
    }

    uint16_t rm[8];
    _mm_storeu_si128( (__m128i*)rm, mul );
    uint16_t sm = _mm_cvtsi128_si64( srcMid );

    uint64_t d = ( uint64_t( sm ) << 56 ) |
        ( uint64_t( rm[GetMulSel( sel )] ) << 52 ) |
        ( uint64_t( sel ) << 48 );

    int offset = 45;
    auto ptr = buf[sel];
    for( int i=0; i<16; i++ )
    {
        d |= uint64_t( *ptr++ ) << offset;
        offset -= 3;
    }

    return _bswap64( d );
#else
    {
        bool solid = true;
        const uint8_t* ptr = src + 1;
        const uint8_t ref = *src;
        for( int i=1; i<16; i++ )
        {
            if( ref != *ptr++ )
            {
                solid = false;
                break;
            }
        }
        if( solid )
        {
            return ref;
        }
    }

    uint8_t min = src[0];
    uint8_t max = src[0];
    for( int i=1; i<16; i++ )
    {
        if( min > src[i] ) min = src[i];
        else if( max < src[i] ) max = src[i];
    }
    int srcRange = max - min;
    int srcMid = min + srcRange / 2;

    int buf[16][16];
    int err = std::numeric_limits<int>::max();
    int sel;
    int selmul;
    for( int r=0; r<16; r++ )
    {
        int mul = ( ( srcRange * g_alphaRange[r] ) >> 16 ) + 1;

        int rangeErr = 0;
        for( int i=0; i<16; i++ )
        {
            const auto srcVal = src[i];

            int idx = 0;
            const auto modVal = g_alpha[r][0] * mul;
            const auto recVal = clampu8( srcMid + modVal );
            int localErr = sq( srcVal - recVal );

            if( localErr != 0 )
            {
                for( int j=1; j<8; j++ )
                {
                    const auto modVal = g_alpha[r][j] * mul;
                    const auto recVal = clampu8( srcMid + modVal );
                    const auto errProbe = sq( srcVal - recVal );
                    if( errProbe < localErr )
                    {
                        localErr = errProbe;
                        idx = j;
                    }
                }
            }

            buf[r][i] = idx;
            rangeErr += localErr;
        }

        if( rangeErr < err )
        {
            err = rangeErr;
            sel = r;
            selmul = mul;
            if( err == 0 ) break;
        }
    }

    uint64_t d = ( uint64_t( srcMid ) << 56 ) |
                 ( uint64_t( selmul ) << 52 ) |
                 ( uint64_t( sel ) << 48 );

    int offset = 45;
    auto ptr = buf[sel];
    for( int i=0; i<16; i++ )
    {
        d |= uint64_t( *ptr++ ) << offset;
        offset -= 3;
    }

    return _bswap64( d );
#endif
}
