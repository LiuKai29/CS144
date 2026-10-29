#include "wrapping_integers.hh"
#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace std;

const uint64_t MOD = 1UL<<32;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  // Your code here.
  return Wrap32 { zero_point + uint32_t( n ) };
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  // Your code here.
  uint64_t inital = zero_point.raw_value_;
  uint64_t seqno = raw_value_;
  uint64_t interval = ( seqno + MOD - inital ) % MOD;
  
  if ( interval >= checkpoint )
    return interval;
  uint32_t k = ( checkpoint - interval ) / MOD;
  uint64_t upper = ( k + 1 ) * MOD + interval;
  uint64_t lower = k * MOD + interval;

  uint64_t result = upper - checkpoint < checkpoint - lower ? upper : lower;

  return result;
}
