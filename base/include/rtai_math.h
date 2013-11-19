/*
 * Copyright (C) 2013 Paolo Mantegazza <mantegazza@aero.polimi.it>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */


#ifndef _RTAI_MATH_H
#define _RTAI_MATH_H

#define CONFIG_RTAI_GLIBC
//#define CONFIG_RTAI_NEWLIB
//#define CONFIG_RTAI_UCLIBC

#define CONFIG_RTAI_KCOMPLEX

#ifdef __KERNEL__

#include <rtai_schedcore.h>

#define kerrno (_rt_whoami()->kerrno)

char *d2str(double d, int dgt, char *str);

#endif 

/**************** BEGIN MACROS TAKEN AND ADAPTED FROM NEWLIB ****************/

#ifndef HUGE_VAL
#define HUGE_VAL (__builtin_huge_val())
#endif

#ifndef HUGE_VALF
#define HUGE_VALF (__builtin_huge_valf())
#endif

#ifndef HUGE_VALL
#define HUGE_VALL (__builtin_huge_vall())
#endif

#ifndef INFINITY
#define INFINITY (__builtin_inff())
#endif

#ifndef NAN
#define NAN (__builtin_nanf(""))
#endif

#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

int __fpclassify(double x);
int __fpclassifyf(float x);
int __signbit(double x);
int __signbitf(float x);

#ifdef CONFIG_RTAI_NEWLIB
int __fpclassifyd(double x);
#define __fpclassify __fpclassifyd
int __signbitd(double x);
#define __signbit __signbitd
#endif

#define fpclassify(__x) \
	((sizeof(__x) == sizeof(float))  ? __fpclassifyf(__x) : \
	__fpclassify(__x))

#ifndef isfinite
#define isfinite(__y) \
	(__extension__ ({int __cy = fpclassify(__y); \
                           __cy != FP_INFINITE && __cy != FP_NAN;}))
#endif

#ifndef isinf
#define isinf(y) (fpclassify(y) == FP_INFINITE)
#endif

#ifndef isnan
#define isnan(y) (fpclassify(y) == FP_NAN)
#endif

#define isnormal(y) (fpclassify(y) == FP_NORMAL)
#define signbit(__x) \
	((sizeof(__x) == sizeof(float))  ?  __signbitf(__x) : \
		__signbit(__x))

#define isunordered(a, b) \
          (__extension__ ({__typeof__(a) __a = (a); __typeof__(b) __b = (b); \
                           fpclassify(__a) == FP_NAN || fpclassify(__b) == FP_NAN;}))

#define isgreater(x, y) \
          (__extension__ ({__typeof__(x) __x = (x); __typeof__(y) __y = (y); \
                           !isunordered(__x,__y) && (__x > __y);}))

#define isgreaterequal(x, y) \
          (__extension__ ({__typeof__(x) __x = (x); __typeof__(y) __y = (y); \
                           !isunordered(__x,__y) && (__x >= __y);}))

#define isless(x, y) \
          (__extension__ ({__typeof__(x) __x = (x); __typeof__(y) __y = (y); \
                           !isunordered(__x,__y) && (__x < __y);}))

#define islessequal(x, y) \
          (__extension__ ({__typeof__(x) __x = (x); __typeof__(y) __y = (y); \
                           !isunordered(__x,__y) && (__x <= __y);}))

#define islessgreater(x, y) \
          (__extension__ ({__typeof__(x) __x = (x); __typeof__(y) __y = (y); \
                           !isunordered(__x,__y) && (__x < __y || __x > __y);}))

/***************** END MACROS TAKEN AND ADAPTED FROM NEWLIB *****************/

#define fperr(x) (!isnormal(x))

int matherr(void);

double acos(double x);
float acosf(float x);
double acosh(double x);
float acoshf(float x);
double asin(double x);
float asinf(float x);
double asinh(double x);
float asinhf(float x);
double atan(double x);
float atanf(float x);
double atan2(double y, double x);
float atan2f(float y, float x);
double atanh(double x);
float atanhf(float x);
double j0(double x);
float j0f(float x);
double j1(double x);
float j1f(float x);
double jn(int n, double x);
float jnf(int n, float x);
double y0(double x);
float y0f(float x);
double y1(double x);
float y1f(float x);
double yn(int n, double x);
float ynf(int n, float x);
double cbrt(double x);
float  cbrtf(float x);
double copysign (double x, double y);
float copysignf (float x, float y);
double cosh(double x);
float coshf(float x);
double erf(double x);
float erff(float x);
double erfc(double x);
float erfcf(float x);
double exp(double x);
float expf(float x);
double exp2(double x);
float exp2f(float x);
double expm1(double x);
float expm1f(float x);
double fabs(double x);
float fabsf(float x);
double fdim(double x, double y);
float fdimf(float x, float y);
double floor(double x);
float floorf(float x);
double ceil(double x);
float ceilf(float x);
double fma(double x, double y, double z);
float fmaf(float x, float y, float z);
double fmax(double x, double y);
float fmaxf(float x, float y);
double fmod(double x, double y);
float fmodf(float x, float y);
double frexp(double val, int *exp);
float frexpf(float val, int *exp);
double gamma(double x);
float gammaf(float x);
double lgamma(double x);
float lgammaf(float x);
double gamma_r(double x, int *signgamp);
float gammaf_r(float x, int *signgamp);
double lgamma_r(double x, int *signgamp);
float lgammaf_r(float x, int *signgamp);
double tgamma(double x);
float tgammaf(float x);
double hypot(double x, double y);
float hypotf(float x, float y);
int ilogb(double val);
int ilogbf(float val);
double infinity(void);
float infinityf(void);
double ldexp(double val, int exp);
float ldexpf(float val, int exp);
double log(double x);
float logf(float x);
double log10(double x);
float log10f(float x);
double log1p(double x);
float log1pf(float x);
double log2(double x);
float log2f(float x);
double logb(double x);
float logbf(float x);
long int lrint(double x);
long int lrintf(float x);
long long int llrint(double x);
long long int llrintf(float x);
long int lround(double x);
long int lroundf(float x);
long long int llround(double x);
long long int llroundf(float x);
double modf(double val, double *ipart);
float modff(float val, float *ipart);
double nan(const char *);
float nanf(const char *);
double nearbyint(double x);
float nearbyintf(float x);
double nextafter(double val, double dir);
float nextafterf(float val, float dir);
double pow(double x, double y);
float powf(float x, float y);
double remainder(double x, double y);
float remainderf(float x, float y);
double remquo(double x, double y, int *quo);
float remquof(float x, float y, int *quo);
double rint(double x);
float rintf(float x);
double round(double x);
float roundf(float x);
double scalbn(double x, int n);
float scalbnf(float x, int n);
double scalbln(double x, long int n);
float scalblnf(float x, long int n);
double sin(double x);
float  sinf(float x);
double cos(double x);
float cosf(float x);
double sinh(double x);
float  sinhf(float x);
double sqrt(double x);
float  sqrtf(float x);
double tan(double x);
float tanf(float x);
double tanh(double x);
float tanhf(float x);
double trunc(double x);
float truncf(float x);

#ifdef CONFIG_RTAI_KCOMPLEX

#define complex _Complex
#define _Complex_I (__extension__ 1.0iF)
#undef I
#define I _Complex_I

double cabs(double _Complex z);
float cabsf(float _Complex z);
asmlinkage double _Complex cacos(double _Complex z);
asmlinkage float _Complex cacosf(float _Complex z);
asmlinkage double _Complex cacosh(double _Complex z);
asmlinkage float _Complex cacoshf(float _Complex z);
double carg(double _Complex z);
float cargf(float _Complex z);
asmlinkage double _Complex casin(double _Complex z);
asmlinkage float _Complex casinf(float _Complex z);
asmlinkage double _Complex casinh(double _Complex z);
asmlinkage float _Complex casinhf(float _Complex z);
asmlinkage double _Complex catan(double _Complex z);
asmlinkage float _Complex catanf(float _Complex z);
asmlinkage double _Complex catanh(double _Complex z);
asmlinkage float _Complex catanhf(float _Complex z);
asmlinkage double _Complex ccos(double _Complex z);
asmlinkage float _Complex ccosf(float _Complex z);
asmlinkage double _Complex ccosh(double _Complex z);
asmlinkage float _Complex ccoshf(float _Complex z);
asmlinkage asmlinkage double _Complex cexp(double _Complex z);
asmlinkage float _Complex cexpf(float _Complex z);
double cimag(double _Complex z);
float cimagf(float _Complex z);
asmlinkage double _Complex clog(double _Complex z);
asmlinkage float _Complex clogf(float _Complex z);
asmlinkage double _Complex conj(double _Complex z);
asmlinkage float _Complex conjf(float _Complex z);
asmlinkage double complex cpow(double complex x, double complex y);
asmlinkage float complex cpowf(float complex x, float complex y);
//asmlinkage double _Complex cpow(double _Complex x, double _Complex y);
//asmlinkage float _Complex cpowf(float _Complex x, float _Complex y);
asmlinkage double _Complex cproj(double _Complex z);
asmlinkage float _Complex cprojf(float _Complex z);
double creal(double _Complex z);
float crealf(float _Complex z);
asmlinkage double _Complex csin(double _Complex z);
asmlinkage float _Complex csinf(float _Complex z);
asmlinkage double _Complex csinh(double _Complex z);
asmlinkage float _Complex csinhf(float _Complex z);
asmlinkage double _Complex csqrt(double _Complex z);
asmlinkage float _Complex csqrtf(float _Complex z);
asmlinkage double _Complex ctan(double _Complex z);
asmlinkage float _Complex ctanf(float _Complex z);
asmlinkage double _Complex ctanh(double _Complex z);
asmlinkage float _Complex ctanhf(float _Complex z);

#endif

#endif /* !_RTAI_MATH_H */
