/* Copyright (c) Colorado School of Mines, 2011.*/
/* All rights reserved.                       */

/* SUGPRFB: $Revision: 1.3 $ ; $Date: 2011/11/17 00:03:38 $	*/

#include "su.h"
#include "segy.h"
#include "header.h"



/*********************** self documentation *****************************/
char *sdoc[] = {
"SUGPRFB - SU program to remove First Breaks from GPR data		",
" 									",
"  sugprfb < radar traces >outfile			  		",
" 									",
" nx=51		number of traces to sum to create pilot trace (odd)	",
" fbt=60	length of first break in number of samples		",
" 									",
" Notes:								",
" The first fbt samples from nx traces are stacked to form a pilot	",
" first break trace, this is fitted to the actual traces by shifting	",
" and scaling.		 The nx traces long spatial window is		",
" slided along the section and a new pilot traces is formed for each	",
" position. The scalers in percent and the time shifts are stored in	",
" header words trwf and grnors.					  	",
" 									",
NULL};
   
/* Segy data constans */
segy 	tr;				/* SEGY trace */

void remove_fb(float *data,float *wavelet,int n,short *scaler,short *shft);

int
main( int argc, char *argv[] )
{
 

	int nx;
	int fbt;
	int nt;
	
	float *stacked=NULL;
	int *nnz=NULL;
	int itr=0;
	
 
	initargs(argc, argv);
   	requestdoc(1);
	
	if (!getparint("nx", &nx)) nx = 51;
	if( !ISODD(nx) ) {
		nx++;
		warn(" nx has been changed to %d to be odd.\n",nx);
	}
	
	if (!getparint("fbt", &fbt)) fbt = 60;
        checkpars();
	
	/* Get info from first trace */ 
	if (!gettr(&tr))  err("can't get first trace");
	nt = tr.ns;
	
	stacked = ealloc1float(fbt);
	nnz = ealloc1int(fbt);
	memset((void *) nnz, (int) '\0', fbt*ISIZE);
	memset((void *) stacked, (int) '\0', fbt*FSIZE);

	/* read nx traces and stack them */
	/* The first trace is already read */
	
	{ int i,it;
	  float **tr_b;
	  char  **hdr_b;
	  int NXP2=nx/2;
	  short shft,scaler;
	  
		/* ramp on read the first nx traces and create stack */
		
	  	tr_b = ealloc2float(nt,nx);
		hdr_b = (char**)ealloc2(HDRBYTES,nx,sizeof(char));
		
		memcpy((void *) hdr_b[0], (const void *) &tr, HDRBYTES);
		memcpy((void *) tr_b[0], (const void *) &tr.data, nt*FSIZE);
		
		for(i=1;i<nx;i++) {
			gettr(&tr);
			memcpy((void *) hdr_b[i], (const void *) &tr, HDRBYTES);
			memcpy((void *) tr_b[i], (const void *) &tr.data, nt*FSIZE);
		}
		
		for(i=0;i<nx;i++) 
			for(it=0;it<fbt;it++) 
				stacked[it] += tr_b[i][it];
		
		
		for(it=0;it<fbt;it++)
			stacked[it] /=(float)nx;
		
			
		/* filter and write out the first nx/2 +1 traces */
		for(i=0;i<NXP2+1;i++) {
			memcpy((void *) &tr, (const void *) hdr_b[i], HDRBYTES);
			memcpy((void *) tr.data, (const void *) tr_b[i], nt*FSIZE);
			
			remove_fb(tr.data,stacked,fbt,&scaler,&shft);
			tr.trwf = scaler;
			tr.grnors = shft;

			puttr(&tr);
			++itr;
		}
		
		/* do the rest of the traces */
		gettr(&tr);
		
		do {
			
			/* Update the stacked trace  - remove old */
			for(it=0;it<fbt;it++) 
				stacked[it] -= tr_b[0][it]/(float)nx;
				
			/* Bump up the storage arrays */
			/* This is not very efficient , but good enough */
			{int ib;
				for(ib=1;ib<nx;ib++) {
				    memcpy((void *) hdr_b[ib-1],
					(const void *) hdr_b[ib], HDRBYTES);
				memcpy((void *) tr_b[ib-1],
					(const void *) tr_b[ib], nt*FSIZE);
				}
			}
			
			/* Store the new trace */
			memcpy((void *) hdr_b[nx-1], (const void *) &tr, HDRBYTES);
			memcpy((void *) tr_b[nx-1], (const void *) &tr.data, nt*FSIZE);
			
			/* Update the stacked array  - add new */
			for(it=0;it<fbt;it++) 
				stacked[it] += tr_b[nx-1][it]/(float)nx;
			
			/* Filter and write out the middle one NXP2+1 */
			memcpy((void *) &tr, (const void *) hdr_b[NXP2], HDRBYTES);
			memcpy((void *) tr.data, (const void *) tr_b[NXP2], nt*FSIZE);
			
			remove_fb(tr.data,stacked,fbt,&scaler,&shft);
			
			tr.trwf = scaler;
			tr.grnors = shft;
			puttr(&tr);
			++itr;
			
			
		} while(gettr(&tr));

		/* Ramp out - write ot the rest of the traces */
		/* filter and write out the last nx/2 traces */
		for(i=NXP2+1;i<nx;i++) {
			memcpy((void *) &tr, (const void *) hdr_b[i], HDRBYTES);
			memcpy((void *) tr.data, (const void *) tr_b[i], nt*FSIZE);
			
			remove_fb(tr.data,stacked,fbt,&scaler,&shft);
			
			tr.trwf = scaler;
			tr.grnors = shft;
			puttr(&tr);
			itr++;
		
		}
		
		
	}
		
  
	
	free1float(stacked);
	free1int(nnz);
   	return EXIT_SUCCESS;
}

void remove_fb(float *yp,float *ym,int n,short *scaler,short *shft)
/* Find Scale and timeshift 

	Yp = data trace
	Ym = wavelet
	
	F=min(Yp(x) - Ym(x)*a)^2 is a function to minimize for scaler
	
	find shift first with xcorrelation then
	
	solve for a - scaler 
*/



{

#define RAMPR 5

	void find_p(float *ym,float *yp,float *a,int *b,int n);
	
	int it,ir;
	
	float a=1.0;
	int b=0;
	int ramps;
	float *a_ramp;
	
	ramps=NINT(n-n/RAMPR);
	
	find_p(ym,yp,&a,&b,n);
	
	a_ramp = ealloc1float(n);
	
	for(it=0;it<ramps;it++)
		a_ramp[it]=1.0;
	
	for(it=ramps,ir=0;it<n;it++,ir++) {
		a_ramp[it]=1.0-(float)ir/(float)(n-ramps-1);
	}
	
	
	
	if (b<0) {
		for(it=0;it<n+b;it++) 
			yp[it-b] -=ym[it]*a*a_ramp[it-b];
	} else {
		for(it=0;it<n-b;it++) 
			yp[it] -=ym[it-b]*a*a_ramp[it+b];
	}
	
	*scaler = NINT((1.0-a)*100.0);
	*shft = b;
	
	free1float(a_ramp);
}

void find_p(float *ym,float *yp,float *a,int *b,int n)
{
#define NP 1

	float *y,**x;
	int n_s;
	
	int k;
	float *res;
	int jpvt[NP];
	float qraux[NP];
	float work[NP]; 
	
														
	x = ealloc2float(n,1);
	y = ealloc1float(n);
	res = ealloc1float(n);
	
	memcpy((void *) &x[0][0], (const void *) &ym[0], n*FSIZE);
	memset((void *) y, (int) '\0', n*FSIZE);	
	
	/* Solve for shift */
 	xcor (n,0,ym,n,0,yp,n,-n/2,y);
	/* pick the maximum */
	*b = -max_index(n,y,1)+n/2;

	n_s = n-abs(*b);
 	
	if (*b < 0) {
		memcpy((void *) &x[0][0], (const void *) &ym[*b], n_s*FSIZE);
	} else {
		memcpy((void *) &x[0][*b], (const void *) &ym[0], n_s*FSIZE);
													
	}
	/* Solve for scaler */
	sqrst(x, n_s, 1,yp,0.0,a,res,&k,&jpvt[0],&qraux[0],&work[0]);
	
	
	
	free2float(x);
	free1float(res);
	free1float(y);
}
