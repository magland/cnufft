#include "finufft.h"
#include "twopispread.h"
#include "common.h"
#include <fftw3.h>
#include <math.h>
#include <stdio.h>
#include <iostream>
#include <iomanip>

int finufft3d1(BIGINT nj,double* xj,double *yj,double *zj,dcomplex* cj,int iflag,
	       double eps, BIGINT ms, BIGINT mt, BIGINT mu, dcomplex* fk,
	       nufft_opts opts)
 /*  Type-1 3D complex nonuniform FFT.

                     1  nj-1
     f[k1,k2,k3] =  --  SUM  c[j] exp(+-i (k1 x[j] + k2 y[j] + k3 z[j]))
                    nj  j=0

	for -ms/2 <= k1 <= (ms-1)/2,  -mt/2 <= k2 <= (mt-1)/2,
            -mu/2 <= k3 <= (mu-1)/2.

     The output array is in increasing k orderings. k1 is fastest, k2 middle,
     and k3 slowest, ie Fortran ordering. If iflag>0 the + sign is
     used, otherwise the - sign is used, in the exponential.
                           
   Inputs:
     nj     number of sources (integer of type BIGINT; see utils.h)
     xj,yj,zj   x,y,z locations of sources on 3D domain [-pi,pi]^3.
     cj     size-nj complex double array of source strengths, 
            (ie, stored as 2*nj doubles interleaving Re, Im).
     iflag  if >0, uses + sign in exponential, otherwise - sign.
     eps    precision requested
     ms,mt,mu  number of Fourier modes requested in x,y,z;
            each may be even or odd;
            in either case the mode range is integers lying in [-m/2, (m-1)/2]
     opts   struct controlling options (see finufft.h)
   Outputs:
     fk     complex double array of Fourier transform values (size ms*mt*mu,
            increasing fast in ms to slowest in mu, ie Fortran ordering).
     returned value - error return code, as returned by cnufftspread:
                      0 : success.

     The type 1 NUFFT proceeds in three main steps (see [GL]):
     1) spread data to oversampled regular mesh using kernel.
     2) compute FFT on uniform mesh
     3) deconvolve by division of each Fourier mode independently by the
        Fourier series coefficient of the kernel.
     The kernel coeffs are precomputed in what is called step 0 in the code.

   Written with FFTW style complex arrays. Barnett 2/2/17
 */
{
  spread_opts spopts;
  int ier_set = setup_kernel(spopts,eps,opts.R);
  if (ier_set) exit(ier_set);
  BIGINT nf1; set_nf_type12(ms,opts,spopts,&nf1);
  BIGINT nf2; set_nf_type12(mt,opts,spopts,&nf2);
  BIGINT nf3; set_nf_type12(mu,opts,spopts,&nf3);
  cout << scientific << setprecision(15);  // for debug

  if (opts.debug) printf("3d1: (ms,mt,mu)=(%ld,%ld,%ld) (nf1,nf2,nf3)=(%ld,%ld,%ld) nj=%ld ...\n",ms,mt,mu,nf1,nf2,nf3,nj); 

  // STEP 0: get DCT of half of spread kernel in each dim, since real symm:
  CNTime timer; timer.start();
  double *fwkerhalf1 = fftw_alloc_real(nf1/2+1);
  double *fwkerhalf2 = fftw_alloc_real(nf2/2+1);
  double *fwkerhalf3 = fftw_alloc_real(nf3/2+1);
  onedim_fseries_kernel(nf1, fwkerhalf1, spopts);
  onedim_fseries_kernel(nf2, fwkerhalf2, spopts);
  onedim_fseries_kernel(nf3, fwkerhalf3, spopts);
  double t=timer.elapsedsec();
  if (opts.debug) printf("kernel fser (ns=%d):\t %.3g s\n", spopts.nspread,t);

  int nth = MY_OMP_GET_MAX_THREADS();
  if (nth>1) {             // set up multithreaded fftw stuff...
    fftw_init_threads();
    fftw_plan_with_nthreads(nth);
  }
  timer.restart();
  fftw_complex *fw = fftw_alloc_complex(nf1*nf2*nf3);  // working upsampled array
  int fftsign = (iflag>0) ? 1 : -1;
  fftw_plan p = fftw_plan_dft_3d(nf3,nf2,nf1,fw,fw,fftsign, FFTW_ESTIMATE);  // in-place
  if (opts.debug) printf("fftw plan\t\t %.3g s\n", timer.elapsedsec());

  // Step 1: spread from irregular points to regular grid
  timer.restart();
  spopts.debug = opts.spread_debug;
  spopts.spread_direction = 1;
  int ier_spread = twopispread3d(nf1,nf2,nf3,(dcomplex*)fw,nj,xj,yj,zj,cj,spopts);
  if (opts.debug) printf("spread (ier=%d):\t\t %.3g s\n",ier_spread,timer.elapsedsec());
  if (ier_spread>0) exit(ier_spread);

  // Step 2:  Call FFT
  timer.restart();
  fftw_execute(p);
  fftw_destroy_plan(p);
  if (opts.debug) printf("fft (%d threads):\t %.3g s\n", nth, timer.elapsedsec());

  // Step 3: Deconvolve by dividing coeffs by that of kernel; shuffle to output
  timer.restart();
  deconvolveshuffle3d(1,1.0/nj,fwkerhalf1,fwkerhalf2,fwkerhalf3,ms,mt,mu,(double*)fk,nf1,nf2,nf3,fw);  // 1/nj prefac
  if (opts.debug) printf("deconvolve & copy out:\t %.3g s\n", timer.elapsedsec());

  fftw_free(fw); fftw_free(fwkerhalf1); fftw_free(fwkerhalf2); fftw_free(fwkerhalf3);
  if (opts.debug) printf("freed\n");
  return 0;
}

int finufft3d2(BIGINT nj,double* xj,double *yj,double *zj,dcomplex* cj,
	       int iflag,double eps, BIGINT ms, BIGINT mt, BIGINT mu,
	       dcomplex* fk, nufft_opts opts)

 /*  Type-2 3D complex nonuniform FFT.

     cj[j] =    SUM   fk[k1,k2,k3] exp(+/-i (k1 xj[j] + k2 yj[j] + k3 zj[j]))
             k1,k2,k3
      for j = 0,...,nj-1
     where sum is over -ms/2 <= k1 <= (ms-1)/2, -mt/2 <= k2 <= (mt-1)/2, 
                       -mu/2 <= k3 <= (mu-1)/2

   Inputs:
     nj     number of sources (integer of type BIGINT; see utils.h)
     xj,yj,zj     x,y,z locations of sources on 3D domain [-pi,pi]^3.
     fk     double complex array of Fourier series values (size ms*mt*mu,
            increasing fastest in ms to slowest in mu, ie Fortran ordering).
            (ie, stored as alternating Re & Im parts, 2*ms*mt*mu doubles)
     iflag  if >0, uses + sign in exponential, otherwise - sign.
     eps    precision requested
     ms,mt,mu  numbers of Fourier modes given in x,y,z; each may be even or odd;
            in either case the mode range is integers lying in [-m/2, (m-1)/2].
     opts   struct controlling options (see finufft.h)
   Outputs:
      cj    size-nj complex double array of target values,
            (ie, stored as 2*nj doubles interleaving Re, Im).
    returned value - error return code, as returned by cnufftspread:
                      0 : success.

     The type 2 algorithm proceeds in three main steps (see [GL]).
     1) deconvolve (amplify) each Fourier mode, dividing by kernel Fourier coeff
     2) compute inverse FFT on uniform fine grid
     3) spread (dir=2, ie interpolate) data to regular mesh
     The kernel coeffs are precomputed in what is called step 0 in the code.

   Written with FFTW style complex arrays. Barnett 2/2/17
 */
{
  spread_opts spopts;
  int ier_set = setup_kernel(spopts,eps,opts.R);
  if (ier_set) exit(ier_set);
  BIGINT nf1; set_nf_type12(ms,opts,spopts,&nf1);
  BIGINT nf2; set_nf_type12(mt,opts,spopts,&nf2);
  BIGINT nf3; set_nf_type12(mu,opts,spopts,&nf3);
  cout << scientific << setprecision(15);  // for debug

  if (opts.debug) printf("3d2: (ms,mt,mu)=(%ld,%ld,%ld) (nf1,nf2,nf3)=(%ld,%ld,%ld) nj=%ld ...\n",ms,mt,mu,nf1,nf2,nf3,nj);

  // STEP 0: get Fourier coeffs of spread kernel in each dim:
  CNTime timer; timer.start();
  double *fwkerhalf1 = fftw_alloc_real(nf1/2+1);
  double *fwkerhalf2 = fftw_alloc_real(nf2/2+1);
  double *fwkerhalf3 = fftw_alloc_real(nf3/2+1);
  onedim_fseries_kernel(nf1, fwkerhalf1, spopts);
  onedim_fseries_kernel(nf2, fwkerhalf2, spopts);
  onedim_fseries_kernel(nf3, fwkerhalf3, spopts);
  double t=timer.elapsedsec();
  if (opts.debug) printf("kernel fser (ns=%d):\t %.3g s\n", spopts.nspread,t);

  int nth = MY_OMP_GET_MAX_THREADS();
  if (nth>1) {             // set up multithreaded fftw stuff...
    fftw_init_threads();
    fftw_plan_with_nthreads(nth);
  }
  timer.restart();
  fftw_complex *fw = fftw_alloc_complex(nf1*nf2*nf3); // working upsampled array
  int fftsign = (iflag>0) ? 1 : -1;
  fftw_plan p = fftw_plan_dft_3d(nf3,nf2,nf1,fw,fw,fftsign, FFTW_ESTIMATE);  // in-place
  if (opts.debug) printf("fftw plan\t\t %.3g s\n", timer.elapsedsec());

  // STEP 1: amplify Fourier coeffs fk and copy into upsampled array fw
  timer.restart();
  deconvolveshuffle3d(2,1.0,fwkerhalf1,fwkerhalf2,fwkerhalf3,ms,mt,mu,(double*)fk,nf1,nf2,nf3,fw);
  if (opts.debug) printf("amplify & copy in:\t %.3g s\n",timer.elapsedsec());

  // Step 2:  Call FFT
  timer.restart();
  fftw_execute(p);
  fftw_destroy_plan(p);
  if (opts.debug) printf("fft (%d threads):\t %.3g s\n",nth,timer.elapsedsec());
  spopts.debug = opts.spread_debug;
  spopts.spread_direction = 2;

  // Step 3: unspread (interpolate) from regular to irregular target pts
  timer.restart();
  spopts.debug = opts.spread_debug;
  spopts.spread_direction = 2;
  int ier_spread = twopispread3d(nf1,nf2,nf3,(dcomplex*)fw,nj,xj,yj,zj,cj,spopts);
  if (opts.debug) printf("unspread (ier=%d):\t %.3g s\n",ier_spread,timer.elapsedsec());
  if (ier_spread>0) exit(ier_spread);

  fftw_free(fw);
  fftw_free(fwkerhalf1); fftw_free(fwkerhalf2); fftw_free(fwkerhalf3);
  if (opts.debug) printf("freed\n");
  return 0;
}

int finufft3d3(BIGINT nj,double* xj,double* yj,double *zj, dcomplex* cj,
	       int iflag, double eps, BIGINT nk, double* s, double *t,
	       double *u, dcomplex* fk, nufft_opts opts)
 /*  Type-3 3D complex nonuniform FFT.

               nj-1
     fk[k]  =  SUM   c[j] exp(+-i (s[k] xj[j] + t[k] yj[j] + u[k] zj[j]),
               j=0
                          for k=0,...,nk-1
   Inputs:
     nj     number of sources (integer of type BIGINT; see utils.h)
     xj,yj,zj   x,y,z location of sources in R^3.
     cj     size-nj complex double array of source strengths
            (ie, interleaving Re & Im parts)
     nk     number of frequency target points
     s,t,u      (k_x,k_y,k_z) frequency locations of targets in R^3.
     iflag  if >0, uses + sign in exponential, otherwise - sign.
     eps    precision requested
     opts   struct controlling options (see finufft.h)
   Outputs:
     fk     size-nk complex double array of Fourier transform values at the
            target frequencies sk
     returned value - error return code, as returned by finufft2d2:
                      0 : success.

     The type 3 algorithm is basically a type 2 (which is implemented precisely
     as call to type 2) replacing the middle FFT (Step 2) of a type 1. See [LG].
     Beyond this, the new twists are:
     i) number of upsampled points for the type-1 in each dim, depends on the
       product of interval widths containing input and output points (X*S), for
       that dim.
     ii) The deconvolve (post-amplify) step is division by the Fourier transform
       of the scaled kernel, evaluated on the *nonuniform* output frequency
       grid; this is done by direct approximation of the Fourier integral
       using quadrature of the kernel function times exponentials.
     iii) Shifts in x (real) and s (Fourier) are done to minimize the interval
       half-widths X and S, hence nf, in each dim.

   No references to FFTW are needed here. Some dcomplex arithmetic is used,
   thus compile with -Ofast in GNU.
   Barnett 2/17/17
 */
{
  spread_opts spopts;
  int ier_set = setup_kernel(spopts,eps,opts.R);
  if (ier_set) exit(ier_set);
  BIGINT nf1,nf2,nf3;
  double X1,C1,S1,D1,h1,gam1,X2,C2,S2,D2,h2,gam2,X3,C3,S3,D3,h3,gam3;
  cout << scientific << setprecision(15);  // for debug

  // pick x, s intervals & shifts, then apply these to xj, cj (twist iii)...
  CNTime timer; timer.start();
  arraywidcen(nj,xj,X1,C1);  // get half-width, center, containing {x_j}
  arraywidcen(nk,s,S1,D1);   // {s_k}
  arraywidcen(nj,yj,X2,C2);  // {y_j}
  arraywidcen(nk,t,S2,D2);   // {t_k}
  arraywidcen(nj,zj,X3,C3);  // {z_j}
  arraywidcen(nk,u,S3,D3);   // {u_k}
  // todo: if C1<X1/10 etc then set C1=0.0 and skip the slow-ish rephasing?
  set_nhg_type3(S1,X1,opts,spopts,&nf1,&h1,&gam1);          // applies twist i)
  set_nhg_type3(S2,X2,opts,spopts,&nf2,&h2,&gam2);
  set_nhg_type3(S3,X3,opts,spopts,&nf3,&h3,&gam3);
  if (opts.debug) printf("3d3: X1=%.3g C1=%.3g S1=%.3g D1=%.3g gam1=%g nf1=%ld X2=%.3g C2=%.3g S2=%.3g D2=%.3g gam2=%g nf2=%ld X3=%.3g C3=%.3g S3=%.3g D3=%.3g gam3=%g nf3=%ld nj=%ld nk=%ld...\n",
	 X1,C1,S1,D1,gam1,nf1,X2,C2,S2,D2,gam2,nf2,X3,C3,S3,D3,gam3,nf3,nj,nk);
  double* xpj = (double*)malloc(sizeof(double)*nj);
  double* ypj = (double*)malloc(sizeof(double)*nj);
  double* zpj = (double*)malloc(sizeof(double)*nj);
  for (BIGINT j=0;j<nj;++j) {
    xpj[j] = (xj[j]-C1) / gam1;          // rescale x_j
    ypj[j] = (yj[j]-C2) / gam2;          // rescale y_j
    zpj[j] = (zj[j]-C3) / gam3;          // rescale z_j
  }
  dcomplex imasign = (iflag>0) ? ima : -ima;
  dcomplex* cpj = (dcomplex*)malloc(sizeof(dcomplex)*nj);  // c'_j rephased src
#pragma omp parallel for schedule(dynamic)                // since cexp slow
  for (BIGINT j=0;j<nj;++j)
    cpj[j] = cj[j] * exp(imasign*(D1*xj[j]+D2*yj[j]+D3*zj[j])); // rephase
  if (opts.debug) printf("prephase:\t\t %.3g s\n",timer.elapsedsec());

  // Step 1: spread from irregular sources to regular grid as in type 1
  dcomplex* fw = (dcomplex*)malloc(sizeof(dcomplex)*nf1*nf2*nf3);
  timer.restart();
  spopts.debug = opts.spread_debug;
  spopts.spread_direction = 1;
  int ier_spread = twopispread3d(nf1,nf2,nf3,fw,nj,xpj,ypj,zpj,cpj,spopts);
  free(xpj); free(ypj); free(zpj); free(cpj);
  if (opts.debug) printf("spread (ier=%d):\t\t %.3g s\n",ier_spread,timer.elapsedsec());
  if (ier_spread>0) exit(ier_spread);

  // Step 2: call type-2 to eval regular as Fourier series at rescaled targs
  timer.restart();
  double *sp = (double*)malloc(sizeof(double)*nk);     // rescaled targs s'_k
  double *tp = (double*)malloc(sizeof(double)*nk);     // t'_k
  double *up = (double*)malloc(sizeof(double)*nk);     // u'_k
  for (BIGINT k=0;k<nk;++k) {
    sp[k] = h1*gam1*(s[k]-D1);                         // so that |s'_k| < pi/R
    tp[k] = h2*gam2*(t[k]-D2);                         // so that |t'_k| < pi/R
    up[k] = h3*gam3*(u[k]-D3);                         // so that |u'_k| < pi/R
  }
  int ier_t2 = finufft3d2(nk,sp,tp,up,fk,iflag,eps,nf1,nf2,nf3,fw,opts);
  free(fw);
  if (opts.debug) printf("total type-2 (ier=%d):\t %.3g s\n",ier_t2,timer.elapsedsec());
  if (ier_t2>0) exit(ier_t2);

  // Step 3a: compute Fourier transform of scaled kernel at targets
  timer.restart();
  double *fkker1 = (double*)malloc(sizeof(double)*nk);
  double *fkker2 = (double*)malloc(sizeof(double)*nk);
  double *fkker3 = (double*)malloc(sizeof(double)*nk);
  // exploit that Fourier transform separates because kernel built separable...
  onedim_nuft_kernel(nk, sp, fkker1, spopts);           // fill fkker1
  onedim_nuft_kernel(nk, tp, fkker2, spopts);           // etc
  onedim_nuft_kernel(nk, up, fkker3, spopts);
  if (opts.debug) printf("kernel FT (ns=%d):\t %.3g s\n", spopts.nspread,timer.elapsedsec());
  // Step 3b: correct for spreading by dividing by the Fourier transform from 3a
  timer.restart();
  // todo: if C1==0.0 don't do the expensive exp()... ?
#pragma omp parallel for schedule(dynamic)              // since cexps slow
  for (BIGINT k=0;k<nk;++k)         // also phases to account for C1,C2,C3 shift
    fk[k] *= (dcomplex)(1.0/(fkker1[k]*fkker2[k]*fkker3[k])) *
      exp(imasign*((s[k]-D1)*C1 + (t[k]-D2)*C2 + (u[k]-D3)*C3));
  if (opts.debug) printf("deconvolve:\t\t %.3g s\n",timer.elapsedsec());

  free(fkker1); free(fkker2); free(fkker3); free(sp);
  if (opts.debug) printf("freed\n");
  return 0;
}
