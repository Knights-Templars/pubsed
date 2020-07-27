#include <stdio.h>
#include <algorithm>
#include <cmath>
#include "brent.h"

// This will work generally for any class where the constraint function being solved is set up to take, as its sole argument, the variable it is solving for.
// That member function is passed here as func
// If func requires additional parameters, those can be handled by the class behind the scences. For example, func may itself call another function which takes the additional parameters as arguments. So, this might require wrapping a pre-existing class member function with a new function, passed here as a func, which takes only the one argument

#define CALL_MEMBER_FN(object,ptrToMember)  ((object).*(ptrToMember)) // This would be better done with std::invoke, but that requires c++17

// implementation of Brent solver, by Paul Duffell
template <class cl>
double brent_solver<cl>::solve(cl& cl_instance, constraintMemFn func, double aa, double bb, double eps, int* n) 
{
  double a = aa;
  double b = bb;

  double fa = CALL_MEMBER_FN(cl_instance,func)(a);
  double fb = CALL_MEMBER_FN(cl_instance,func)(b);
  *n = 0;
  if( fa*fb >= 0 ){
    printf("Brent failed; not bracketed properly\n");
    *n = -1;
    return(0.0);
  }
  if( fabs(fa) < fabs(fb) ){
    double temp = a;
    a = b;
    b = temp;
    temp = fa;
    fa = fb;
    fb = temp;
  }
  double c = a;
  double s = a;
  int stillrunning = 1;
  while( stillrunning ){
    //printf("%e %e %e %e %d\n",c,func(b),a,b,*n); // debugging output 
    //    printf("%e %e %e %e %d\n",c,CALL_MEMBER_FN(cl_instance,func)(b),a,b,*n); //debugging output
    double fc = CALL_MEMBER_FN(cl_instance,func)(c);
    if( fa != fc && fb != fc ){
      s = a*fb*fc/(fa-fb)/(fa-fc) + b*fc*fa/(fb-fc)/(fb-fa) + c*fa*fb/(fc-fa)/(fc-fb);
    }else{
      s = b - fb*(b-a)/(fb-fa);
    }

    double m = 0.5*(a+b);
    int cond = (s-a)*(s-b) >= 0;
    if( cond ){ s=m; }

    double fs = CALL_MEMBER_FN(cl_instance,func)(s);
      
    c = b;

    if( fa*fs < 0 ){ 
      b = s;
      fb = CALL_MEMBER_FN(cl_instance,func)(b);
    }else{
      a = s;
      fa = CALL_MEMBER_FN(cl_instance,func)(a);
    }

    if( fabs(fa) < fabs(fb) ){
      double temp = a; 
      a = b; 
      b = temp;
      temp = fa;
      fa = fb;
      fb = temp;      
    }
      
    *n = *n+1;
    if( fb == 0 || fs == 0 || fabs(b-a) < eps * std::min(fabs(a),fabs(b)) ) stillrunning = 0;

  }
  return( b );


}

