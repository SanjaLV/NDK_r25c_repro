#include <assert.h>
#include <limits.h> // for CHAR_BIT
#include <stdio.h>
#include <stdlib.h>

////////////////////////////////////////////////////////////////////////

#define USE_MONTGOMERY_SETUP 0
#define STRIP_NOT_REACHABLE_FUNCTION 1
#define MAKE_COMPILER_CRASH 0

////////////////////////////////////////////////////////////////////////

typedef unsigned int mp_digit;
typedef unsigned long long mp_word;

#define NO_INLINE __attribute__((noinline))

#define MP_ZPOS 0
#define MP_NEG 1

#define MP_OKAY 0 /* ok result */
#define MP_MEM -2 /* out of mem */
#define MP_VAL -3 /* invalid input */
#define MP_RANGE MP_VAL

#define DIGIT_BIT 28
#define MP_PREC 32

#define MP_MASK ((((mp_digit)1) << ((mp_digit)DIGIT_BIT)) - ((mp_digit)1))
#define MP_WARRAY (1 << (sizeof(mp_word) * CHAR_BIT - 2 * DIGIT_BIT + 1))

typedef struct {
  int used, alloc, sign;
  mp_digit *dp;
} mp_int;

// AD-HOC function for printing/debugging mp_ints
void NO_INLINE mp_int_print(mp_int *x, char *name) {
  printf("%s:\n", name);
  printf("\tsign = %d\n", x->sign);
  printf("\tused = %d\n", x->used);
  printf("\talloc = %d\n", x->alloc);
  printf("\tdp = { ");
  for (int i = 0; i < x->used; i++) {
    printf("0x%x ", x->dp[i]);
  }
  printf("}\n");
}

static void NO_INLINE mp_clamp(mp_int *a) {
  /* decrease used while the most significant digit is
   * zero.
   */
  while (a->used > 0 && a->dp[a->used - 1] == 0) {
    --(a->used);
  }

  /* reset the sign flag if used == 0 */
  if (a->used == 0) {
    a->sign = MP_ZPOS;
  }
}

#if STRIP_NOT_REACHABLE_FUNCTION == 0

#define MP_LT -1
#define MP_EQ 0
#define MP_GT 1

static int mp_grow(mp_int *a, int size) {
  int i;
  mp_digit *tmp;

  /* if the alloc size is smaller alloc more ram */
  if (a->alloc < size) {
    /* ensure there are always at least MP_PREC digits extra on top */
    size += (MP_PREC * 2) - (size % MP_PREC);

    /* reallocate the array a->dp
     *
     * We store the return in a temporary variable
     * in case the operation failed we don't want
     * to overwrite the dp member of a.
     */
    tmp = realloc(a->dp, sizeof(mp_digit) * size);
    if (tmp == NULL) {
      /* reallocation failed but "a" is still valid [can be freed] */
      return MP_MEM;
    }

    /* reallocation succeeded so set a->dp */
    a->dp = tmp;

    /* zero excess digits */
    i = a->alloc;
    a->alloc = size;
    for (; i < a->alloc; i++) {
      a->dp[i] = 0;
    }
  }
  return MP_OKAY;
}

static int mp_cmp_mag(mp_int *a, mp_int *b) {
  int n;
  mp_digit *tmpa, *tmpb;

  /* compare based on # of non-zero digits */
  if (a->used > b->used) {
    return MP_GT;
  }

  if (a->used < b->used) {
    return MP_LT;
  }

  /* alias for a */
  tmpa = a->dp + (a->used - 1);

  /* alias for b */
  tmpb = b->dp + (a->used - 1);

  /* compare based on digits  */
  for (n = 0; n < a->used; ++n, --tmpa, --tmpb) {
    if (*tmpa > *tmpb) {
      return MP_GT;
    }

    if (*tmpa < *tmpb) {
      return MP_LT;
    }
  }
  return MP_EQ;
}

static int s_mp_sub(mp_int *a, mp_int *b, mp_int *c) {
  int olduse, res, min, max;

  /* find sizes */
  min = b->used;
  max = a->used;

  /* init result */
  if (c->alloc < max) {
    if ((res = mp_grow(c, max)) != MP_OKAY) {
      return res;
    }
  }
  olduse = c->used;
  c->used = max;

  {
    register mp_digit u, *tmpa, *tmpb, *tmpc;
    register int i;

    /* alias for digit pointers */
    tmpa = a->dp;
    tmpb = b->dp;
    tmpc = c->dp;

    /* set carry to zero */
    u = 0;
    for (i = 0; i < min; i++) {
      /* T[i] = A[i] - B[i] - U */
      *tmpc = *tmpa++ - *tmpb++ - u;

      /* U = carry bit of T[i]
       * Note this saves performing an AND operation since
       * if a carry does occur it will propagate all the way to the
       * MSB.  As a result a single shift is enough to get the carry
       */
      u = *tmpc >> ((mp_digit)(CHAR_BIT * sizeof(mp_digit) - 1));

      /* Clear carry from T[i] */
      *tmpc++ &= MP_MASK;
    }

    /* now copy higher words if any, e.g. if A has more digits than B  */
    for (; i < max; i++) {
      /* T[i] = A[i] - U */
      *tmpc = *tmpa++ - u;

      /* U = carry bit of T[i] */
      u = *tmpc >> ((mp_digit)(CHAR_BIT * sizeof(mp_digit) - 1));

      /* Clear carry from T[i] */
      *tmpc++ &= MP_MASK;
    }

    /* clear digits above used (since we may not have grown result above) */
    for (i = c->used; i < olduse; i++) {
      *tmpc++ = 0;
    }
  }

  mp_clamp(c);
  return MP_OKAY;
}

#endif

#if USE_MONTGOMERY_SETUP == 1

static int NO_INLINE mp_montgomery_setup(mp_int *n, mp_digit *rho) {
  mp_digit x, b;

  /* fast inversion mod 2**k
   *
   * Based on the fact that
   *
   * XA = 1 (mod 2**n)  =>  (X(2-XA)) A = 1 (mod 2**2n)
   *                    =>  2*X*A - X*X*A*A = 1
   *                    =>  2*(1) - (1)     = 1
   */
  b = n->dp[0];

  if ((b & 1) == 0) {
    return MP_VAL;
  }

  x = (((b + 2) & 4) << 1) + b; /* here x*a==1 mod 2**4 */
  x *= 2 - b * x;               /* here x*a==1 mod 2**8 */
  x *= 2 - b * x;               /* here x*a==1 mod 2**16 */
  x *= 2 - b * x;               /* here x*a==1 mod 2**32 */

  /* rho = -1/m mod b */
  *rho = (unsigned long)(((mp_word)1 << ((mp_word)DIGIT_BIT)) - x) & MP_MASK;

  return MP_OKAY;
}

#endif

#if MAKE_COMPILER_CRASH == 1
#define FAST_MP_ATTRIBUTE NO_INLINE
#else
#define FAST_MP_ATTRIBUTE
#endif

static int FAST_MP_ATTRIBUTE fast_mp_montgomery_reduce(mp_int *x, mp_int *n,
                                                       mp_digit rho) {
  int ix, res, olduse;
  mp_word W[MP_WARRAY];

  /* get old used count */
  olduse = x->used;

  /* grow a as required */
  if (x->alloc < n->used + 1) {
#if STRIP_NOT_REACHABLE_FUNCTION == 0
    if ((res = mp_grow(x, n->used + 1)) != MP_OKAY) {
      return res;
    }
#else
    __builtin_trap();
#endif
  }

  /* first we have to get the digits of the input into
   * an array of double precision words W[...]
   */
  {
    register mp_word *_W;
    register mp_digit *tmpx;

    /* alias for the W[] array */
    _W = W;

    /* alias for the digits of  x*/
    tmpx = x->dp;

    /* copy the digits of a into W[0..a->used-1] */
    for (ix = 0; ix < x->used; ix++) {
      *_W++ = *tmpx++;
    }

    /* zero the high words of W[a->used..m->used*2] */
    for (; ix < n->used * 2 + 1; ix++) {
      *_W++ = 0;
    }
  }

  /* now we proceed to zero successive digits
   * from the least significant upwards
   */
  for (ix = 0; ix < n->used; ix++) {
    /* mu = ai * m' mod b
     *
     * We avoid a double precision multiplication (which isn't required)
     * by casting the value down to a mp_digit.  Note this requires
     * that W[ix-1] have  the carry cleared (see after the inner loop)
     */
    register mp_digit mu;
    mu = (mp_digit)(((W[ix] & MP_MASK) * rho) & MP_MASK);

    /* a = a + mu * m * b**i
     *
     * This is computed in place and on the fly.  The multiplication
     * by b**i is handled by offsetting which columns the results
     * are added to.
     *
     * Note the comba method normally doesn't handle carries in the
     * inner loop In this case we fix the carry from the previous
     * column since the Montgomery reduction requires digits of the
     * result (so far) [see above] to work.  This is
     * handled by fixing up one carry after the inner loop.  The
     * carry fixups are done in order so after these loops the
     * first m->used words of W[] have the carries fixed
     */
    {
      register int iy;
      register mp_digit *tmpn;
      register mp_word *_W;

      /* alias for the digits of the modulus */
      tmpn = n->dp;

      /* Alias for the columns set by an offset of ix */
      _W = W + ix;

      /* inner loop */
      for (iy = 0; iy < n->used; iy++) {
        *_W++ += ((mp_word)mu) * ((mp_word)*tmpn++);
      }
    }

    /* now fix carry for next digit, W[ix+1] */
    W[ix + 1] += W[ix] >> ((mp_word)DIGIT_BIT);
  }

  /* now we have to propagate the carries and
   * shift the words downward [all those least
   * significant digits we zeroed].
   */
  {
    register mp_digit *tmpx;
    register mp_word *_W, *_W1;

    /* nox fix rest of carries */

    /* alias for current word */
    _W1 = W + ix;

    /* alias for next word, where the carry goes */
    _W = W + ++ix;

    for (; ix < n->used * 2 + 1; ix++) {
      *_W++ += *_W1++ >> ((mp_word)DIGIT_BIT);
    }

    /* copy out, A = A/b**n
     *
     * The result is A/b**n but instead of converting from an
     * array of mp_word to mp_digit than calling mp_rshd
     * we just copy them in the right order
     */

    /* alias for destination word */
    tmpx = x->dp;

    /* alias for shifted double precision result */
    _W = W + n->used;

    for (ix = 0; ix < n->used + 1; ix++) {
      *tmpx++ = (mp_digit)(*_W++ & ((mp_word)MP_MASK));
    }

    /* zero oldused digits, if the input a was larger than
     * m->used+1 we'll have to clear the digits
     */
    for (; ix < olduse; ix++) {
      *tmpx++ = 0;
    }
  }

  /* set the max used and clamp */
  x->used = n->used + 1;
  mp_clamp(x);

  /* if A >= m then A = A - m */
#if STRIP_NOT_REACHABLE_FUNCTION == 0
  if (mp_cmp_mag(x, n) != MP_LT) {
    return s_mp_sub(x, n, x);
  }
#endif
  return MP_OKAY;
}

static int NO_INLINE mp_montgomery_reduce(mp_int *x, mp_int *n, mp_digit rho) {
  int ix, res, digs;
  mp_digit mu;

  /* can the fast reduction [comba] method be used?
   *
   * Note that unlike in mul you're safely allowed *less*
   * than the available columns [255 per default] since carries
   * are fixed up in the inner loop.
   */
  digs = n->used * 2 + 1;
  if ((digs < MP_WARRAY) &&
      n->used < (1 << ((CHAR_BIT * sizeof(mp_word)) - (2 * DIGIT_BIT)))) {
    return fast_mp_montgomery_reduce(x, n, rho);
  }

  /* Striped rest of the function, as unreachable */
  __builtin_trap();
}

static int NO_INLINE montgomery_reduce_test() {
  mp_int a;
  {
    a.sign = 0;
    a.used = 14;
    a.alloc = 64;
    a.dp = calloc(64, sizeof(unsigned int));
    a.dp[0] = 0xe6c6d90u;
    a.dp[1] = 0x7f76a7u;
    a.dp[2] = 0x8d200ccu;
    a.dp[3] = 0x6e9ca88u;
    a.dp[4] = 0x7d707eau;
    a.dp[5] = 0xca308d6u;
    a.dp[6] = 0xac266a9u;
    a.dp[7] = 0x794ff0bu;
    a.dp[8] = 0x19b5252u;
    a.dp[9] = 0x5e0118fu;
    a.dp[10] = 0xceaeedeu;
    a.dp[11] = 0x61c5f5du;
    a.dp[12] = 0xfe1ea6cu;
    a.dp[13] = 0x8u;
  }

  mp_int P;
  mp_digit mp;

  int ret;

#if USE_MONTGOMERY_SETUP == 1
  {
    P.sign = 0;
    P.used = 7;
    P.alloc = 32;
    P.dp = calloc(32, sizeof(unsigned int));
    P.dp[0] = 13128331u;
    P.dp[1] = 12345u;
    P.dp[2] = 67890u;
    P.dp[3] = 14680u;
    P.dp[4] = 97531u;
    P.dp[5] = 371u;
    P.dp[6] = 255001u;
  }

  ret = mp_montgomery_setup(&P, &mp);
  assert(ret == 0);

  const int RHO = 251656925;
  assert(mp == RHO);
  {
    assert(P.sign == 0);   // P.sign = 0;
    assert(P.used == 7);   // P.used = 7;
    assert(P.alloc == 32); // P.alloc = 32;
    // P.dp = calloc(32, sizeof(unsigned int));
    assert(P.dp[0] == 0xc8528bu); // P.dp[0] = 0xc8528bu;
    assert(P.dp[1] == 0x3039u);   // P.dp[1] = 0x3039u;
    assert(P.dp[2] == 0x10932u);  // P.dp[2] = 0x10932u;
    assert(P.dp[3] == 0x3958u);   // P.dp[3] = 0x3958u;
    assert(P.dp[4] == 0x17cfbu);  // P.dp[4] = 0x17cfbu;
    assert(P.dp[5] == 0x173u);    // P.dp[5] = 0x173u;
    assert(P.dp[6] == 0x3e419u);  // P.dp[6] = 0x3e419u;
  }
#else
  const int RHO = 251656925;
  mp = RHO;
  {
    P.sign = 0;
    P.used = 7;
    P.alloc = 32;
    P.dp = calloc(32, sizeof(unsigned int));
    P.dp[0] = 0xc8528bu;
    P.dp[1] = 0x3039u;
    P.dp[2] = 0x10932u;
    P.dp[3] = 0x3958u;
    P.dp[4] = 0x17cfbu;
    P.dp[5] = 0x173u;
    P.dp[6] = 0x3e419u;
  }
#endif

  ret = mp_montgomery_reduce(&a, &P, mp);
  assert(ret == 0);

  mp_int_print(&a, "reduce result");

  return 0;
}

int main() {
  int ret = 0;
  ret |= montgomery_reduce_test();
  return ret;
}

