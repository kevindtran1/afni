#ifdef USE_OMP
#include <omp.h>
#endif

#include "mrilib.h"

/***** 3dREMLfit.c *****/

/* [PT: Dec 18, 2020] Online links to Bob's notes have been migrated here:
   https://afni.nimh.nih.gov/pub/dist/doc/htmldoc/statistics/remlfit.html

   He still has an internal directory of useful things here:
   https://afni.nimh.nih.gov/pub/dist/doc/misc/3dREMLfit/
   ... which is mostly linked within the aforementioned link in the
   main AFNI RST pages.
*/


#ifdef USE_OMP
   int maxthr = 1 ;  /* max number of threads [16 Jun 2009] */
#else
# define maxthr 1    /* no OpenMP ==> 1 thread by definition */
#endif

static int verb=1 ;
static int goforit=0 ;

#include "remla.c"

#undef  INMASK
#define INMASK(i) ( mask[i] != 0 )

#undef MEMORY_CHECK
#if defined(USING_MCW_MALLOC) && !defined(USE_OMP)
# define MEMORY_CHECK                                             \
   do{ long long nb = mcw_malloc_total() ;                        \
       if( nb > 0 && verb > 1 )                                   \
         ININFO_message("Memory usage now = %s (%s)" ,            \
                        commaized_integer_string(nb) ,            \
                        approximate_number_string((double)nb) ) ; \
   } while(0)
#else
# define MEMORY_CHECK /*nada*/
#endif

#undef REML_DEBUG

/*---------------------------------------------------------------------------*/
/*! Check matrix condition number. Return value is the
    number of bad things that were detected. Hopefully, zero.
-----------------------------------------------------------------------------*/

int check_matrix_condition( matrix xdata , char *xname )
{
    double *ev, ebot,emin,emax ;
    int i,nsmall=0, ssing=(verb>2), bad=0 ;

#undef  PSINV_EPS
#define PSINV_EPS      1.e-14  /* double precision */
#define CN_VERYGOOD   20.0
#define CN_GOOD      400.0
#define CN_OK       8000.0
#define CN_BEWARE 160000.0

    if( xname == NULL ) xname = "\0" ;
    ev   = matrix_singvals( xdata ) ;
    emax = ev[0] ;
    for( i=1 ; i < xdata.cols ; i++ ) if( ev[i] > emax ) emax = ev[i] ;
    ebot = sqrt(PSINV_EPS)*emax ; emin = 1.e+38 ;
    for( i=0 ; i < xdata.cols ; i++ ){
      if( ev[i] >= ebot && ev[i] < emin ) emin = ev[i] ;
      if( ev[i] <  ebot ) nsmall++ ;
    }

    if( emin <= 0.0 || emax <= 0.0 ){
      ERROR_message("----- !! %s matrix condition:  UNDEFINED ** VERY BAD **\n" ,
                     xname ) ;
      ssing = 1 ; bad++ ;
    } else {
      double cond=sqrt(emax/emin) ; char *rating ;
           if( cond < CN_VERYGOOD )  rating = "++ VERY GOOD ++"    ;
      else if( cond < CN_GOOD     )  rating = "++ GOOD ++"         ;
      else if( cond < CN_OK       )  rating = "++ OK ++"           ;
      else if( cond < CN_BEWARE   )  rating = "++ A LITTLE BIG ++" ;
      else                         { rating = "** BEWARE **" ; bad++; ssing = 1; }
      if( strstr(rating,"*") == NULL ){
        if( verb > 1 ){
          INFO_message("----- %s matrix condition (%dx%d):  %g  %s\n",
                  xname,
                  xdata.rows,xdata.cols , cond , rating ) ;
        }
      } else {
        WARNING_message("----- !! %s matrix condition (%dx%d):  %g  %s\n",
                xname,
                xdata.rows,xdata.cols , cond , rating ) ;
      }
    }

    if( nsmall > 0 ){
      WARNING_message(
        "!! in %s matrix:\n"
        " * Largest singular value=%g\n"
        " * %d singular value%s less than cutoff=%g\n"
        " * Implies strong collinearity in the matrix columns! \n",
        xname , emax , nsmall , (nsmall==1)?" is":"s are" , ebot ) ;
      ssing = 1 ; bad++ ;
    }

    if( ssing ){
      INFO_message("%s matrix singular values:\n",xname) ;
      for( i=0; i < xdata.cols ; i++ ){
        fprintf(stderr," %13g",ev[i]) ;
        if( i < xdata.cols-1 && i%5 == 4 ) fprintf(stderr,"\n") ;
      }
      fprintf(stderr,"\n") ;
    }

    free((void *)ev) ; return bad ;
}

/*---------------------------------------------------------------------------*/
/*! For voxel loop progress report. */

static int vn=0 ;
static void vstep_print(void)
{
   static char xx[10] = "0123456789" ;
   fprintf(stderr , "%c" , xx[vn%10] ) ;
   if( vn%10 == 9) fprintf(stderr,".") ;
   vn++ ;
}

/*--------------------------------------------------------------------------*/

static int    Argc ;
static char **Argv ;
static char *commandline = NULL ;  /* from 3dDeconvolve */

static int usetemp = 0 ;

#undef  MTHRESH
#define MTHRESH (int64_t)7654321  /* 7.6+ million bytes */

/*--------------------------------------------------------------------------*/
/*! To create an empty dataset, with zero-filled sub-bricks,
    or with the data to be saved on disk temporarily (if fnam, fp != NULL). */

THD_3dim_dataset * create_float_dataset( THD_3dim_dataset *tset ,
                                         int nvol , char *prefix, int func ,
                                         char **fnam , FILE **fp            )
{
   THD_3dim_dataset *nset ; int kk ;

ENTRY("create_float_dataset") ;

   if( tset == NULL || nvol < 1 || prefix == NULL ) RETURN(NULL) ;

   nset = EDIT_empty_copy( tset ) ;
   EDIT_dset_items( nset ,
                      ADN_prefix    , prefix    ,
                      ADN_datum_all , MRI_float ,
                      ADN_brick_fac , NULL      ,
                      ADN_nvals     , nvol      ,
                      ADN_ntt       , nvol      ,
                    ADN_none ) ;
  if( func ) EDIT_TO_FUNC_BUCKET(nset) ;

  tross_Copy_History( tset , nset ) ;
  if( commandline != NULL )
    tross_multi_Append_History( nset , "Matrix source:" , commandline , NULL ) ;
  tross_Make_History( "3dREMLfit" , Argc,Argv , nset ) ;

  if( usetemp && fnam != NULL && fp != NULL &&
      DSET_TOTALBYTES(nset) > MTHRESH         ){
    reml_setup_savfilnam( fnam ) ;
    *fp = fopen( *fnam , "w+b" ) ;
    if( *fp == NULL ) ERROR_exit("3dREMLfit cannot open -usetemp file %s",*fnam);
    add_purge(*fnam) ;
  } else {
    for( kk=0 ; kk < nvol ; kk++ )
      EDIT_substitute_brick( nset , kk , MRI_float , NULL ) ;
  }

  RETURN(nset) ;
}

/*----------------------------------------------------------------------------*/
/*! Save a series of floats into a dataset,
    either directly into the bricks, or to a temp file to be restored later. */

char sslab[256] = "\0" ;  /* purely for debugging purposes */

void save_series( int vv, THD_3dim_dataset *dset, int npt, float *var, FILE *fp )
{
ENTRY("save_series") ;
if( sslab[0] != '\0' ){ STATUS(sslab) ; }

   if( fp == NULL )
     THD_insert_series( vv , dset , npt , MRI_float , var , 0 ) ;
   else
     my_fwrite( var , sizeof(float) , npt , fp ) ;

   EXRETURN ;
}

/*--------------------------------------------------------------------------*/
/*! Check a dataset for float errors. */

void check_dataset( THD_3dim_dataset *dset )
{
   int nn ; static int first=1 ;
   nn = dset_floatscan( dset ) ;
   if( nn > 0 ){
     WARNING_message("Zero-ed %d float error%s in dataset %s" ,
                     nn , (nn==1) ? "\0" : "s" , DSET_BRIKNAME(dset) ) ;
     if( first ){
       ININFO_message("   Float errors include NaN and Infinity values") ;
       ININFO_message("   ==> Check matrix setup! Check inputs! Check results!") ;
       first = 0 ;
     }
   }
   return ;
}

/*----------------------------------------------------------------------------*/
/*! If fp!=NULL, load the dataset's data from that file (-usetemp).
    Otherwise, there's nothing to do, so vamoose the ranch.
    [19 Dec 2008] Also, check the dataset for float errors. */

void populate_dataset( THD_3dim_dataset *dset, byte *mask,
                                               char *fnam, FILE *fp  )
{
   int kk,vv=-1 , nvox , nvals , bb ;
   float *var ;

ENTRY("populate_dataset") ;

   if( !ISVALID_DSET(dset) || fp == NULL ){  /* fp != NULL means to */
     check_dataset(dset) ; EXRETURN ;        /* read data from file */
   }

   /* create empty bricks to receive data from file */

   nvals = DSET_NVALS(dset) ;
   for( kk=0 ; kk < nvals ; kk++ )
     EDIT_substitute_brick( dset , kk , MRI_float , NULL ) ;

   if( mask == NULL || fp == NULL ) EXRETURN ;  /* should not happen */

   if( verb > 1 && fnam != NULL ){
     INFO_message("Populating dataset %s from file %s",
                  DSET_FILECODE(dset) , fnam ) ;
     MEMORY_CHECK ;
   }

   var  = (float *)calloc(sizeof(float),nvals) ; /* space for 1 voxel's data */
   nvox = DSET_NVOX(dset) ;

   kk = fseek(fp,0,SEEK_SET) ;
   if( kk != 0 ) perror("rewind") ;

   /* load data from file */

   for( kk=0 ; kk < nvox ; kk++ ){
     if( !INMASK(kk) ) continue ;
     (void)fread( var , sizeof(float) , nvals , fp ) ;
     THD_insert_series( kk , dset , nvals , MRI_float , var , 0 ) ;
   }

   /* done */

   free(var) ; fclose(fp) ;
   if( fnam != NULL ){ kill_purge(fnam) ; remove(fnam) ; }
   check_dataset(dset) ; EXRETURN ;
}

/*--------------------------------------------------------------------------*/
/*! This function creates a G matrix (for GLTs) that selects a subset
    of the full model;
     - mpar = number of parameters in the full model
            = number of columns in the output matrix
     - nrow = number of parameters of the subset model
            = number of rows in the output matrix
     - set  = array of indexes to select (nrow of them)
*//*------------------------------------------------------------------------*/

matrix * create_subset_matrix( int mpar , int nrow , int *set )
{
   matrix *gm ; int ii,jj ;

ENTRY("create_subset_matrix") ;

   if( mpar < 1 || nrow < 1 || nrow > mpar || set == NULL ) RETURN(NULL) ;

   gm = (matrix *)malloc(sizeof(matrix)) ; matrix_initialize(gm) ;
   matrix_create( nrow , mpar , gm ) ;
   for( ii=0 ; ii < nrow ; ii++ ){    /* zero out entire matrix */
     for( jj=0 ; jj < mpar ; jj++ ) gm->elts[ii][jj] = 0.0 ;
   }
   for( ii=0 ; ii < nrow ; ii++ ){
     if( set[ii] < 0 || set[ii] >= mpar ){ free(gm); RETURN(NULL); }
     for( jj=0 ; jj < ii ; jj++ )
       if( set[ii] == set[jj] ){ free(gm); RETURN(NULL); }
     gm->elts[ii][set[ii]] = 1.0 ;  /* ii-th row selects set[ii] param */
   }
   RETURN(gm) ;
}

/*--------------------------------------------------------------------------*/
/* struct for putting values from a GLT into a bucket dataset:
     nrow        = number of rows = number of beta and ttst values (each)
     ivbot       = first index in dataset to get a value from this GLT
     ivtop       = last index in dataset to get a value from this GLT
     beta_ind[i] = index in dataset for i-th beta value (i=0..nrow-1)
                   N.B.: beta_ind == NULL if beta values not to be saved
     beta_lab[i] = label for i-th beta value
     ttst_ind[i] = index in dataset for i-th ttst value
                   N.B.: ttst_ind == NULL if ttst values not to be saved
     ttst_lab[i] = label for i-th beta value
     ftst_ind    = index for ftst value (-1 if not to be saved)
     ftst_lab    = label for ftst value
     rtst_ind    = index for R^2 value (-1 if not to be saved)
     rtst_lab    = label for R^2 value
  Note that the 'built-in' GLTs for each stimulus (group of regressors) have
  as their matrices 0-1 callouts of the appropriate regression coefficients.
  So their GLT coefficients returned will just be the regression
  coefficients.
----------------------------------------------------------------------------*/

typedef struct {
  int nrow , ivbot , ivtop ;
  int   *beta_ind ,  *ttst_ind ,  ftst_ind ,  rtst_ind ;
  char **beta_lab , **ttst_lab , *ftst_lab , *rtst_lab ;
} GLT_index ;

/*-------------------------------------------------------------------------*/
/*! Create a struct for putting values from a GLT into a bucket dataset:
     * ivfirst = index of first sub-brick to get a value
     * nrow    = number of rows in the GLT
     * do_beta = whether to include beta values in the output
     * do_ttst = whether to include t-statistics in the output
     * do_ftst = whether to include F-statistic in the output
     * do_rtst = whether to include R^2 statistic in the output
     * name    = prefix name for this GLT (for sub-brick labels)
*//*-----------------------------------------------------------------------*/

GLT_index * create_GLT_index( int ivfirst , int nrow ,
                              int do_beta , int do_ttst ,
                              int do_ftst , int do_rtst , char *name )
{
   GLT_index *gin ; char lll[256] ; int ii,iv=ivfirst ;

ENTRY("create_GLT_index") ;

   if( !do_beta && !do_ttst && !do_ftst && !do_rtst ) RETURN(NULL) ; /* bad */
   if( ivfirst < 0 || nrow < 1 || name == NULL      ) RETURN(NULL) ;

   gin = (GLT_index *)calloc(1,sizeof(GLT_index)) ;

   gin->nrow = nrow ;

   if( do_beta ){
     gin->beta_ind = (int *  )calloc( sizeof(int)    , nrow ) ;
     gin->beta_lab = (char **)calloc( sizeof(char *) , nrow ) ;
   }
   if( do_ttst ){
     gin->ttst_ind = (int *  )calloc( sizeof(int)    , nrow ) ;
     gin->ttst_lab = (char **)calloc( sizeof(char *) , nrow ) ;
   }

   /* add Coef and Tstat entries, alternating if both are used */

   if( do_beta || do_ttst ){
     for( ii=0 ; ii < nrow ; ii++ ){
       if( do_beta ){
         gin->beta_ind[ii] = iv++ ;
         sprintf( lll , "%.48s%c%d_Coef" , name , EDIT_get_index_prefix() , ii ) ;
         gin->beta_lab[ii] = strdup(lll) ;
       }
       if( do_ttst ){
         gin->ttst_ind[ii] = iv++ ;
         sprintf( lll , "%.48s%c%d_Tstat" , name , EDIT_get_index_prefix() , ii ) ;
         gin->ttst_lab[ii] = strdup(lll) ;
       }
     }
   }

   if( do_rtst ){                  /* 23 Oct 2008 */
     gin->rtst_ind = iv++ ;
     sprintf( lll , "%.48s_R^2" , name ) ;
     gin->rtst_lab = strdup(lll) ;
   } else {
     gin->rtst_ind = -1 ;
     gin->rtst_lab = NULL ;
   }

   if( do_ftst ){
     gin->ftst_ind = iv++ ;
     sprintf( lll , "%.48s_Fstat" , name ) ;
     gin->ftst_lab = strdup(lll) ;
   } else {
     gin->ftst_ind = -1 ;
     gin->ftst_lab = NULL ;
   }

   gin->ivbot = ivfirst ; gin->ivtop = iv-1 ; RETURN(gin) ;
}

/*-------------------------------------------------------------------------*/

static int        nSymStim = 0    ;
static SYM_irange *SymStim = NULL ;

/*! Create a GLT matrix from the symbolic string. */

matrix * create_gltsym( char *sym , int ncol )
{
   int ii,jj , nr=0 , iv ;
   floatvecvec *fvv ;
   float **far=NULL ;
   char *str_echo=NULL ;
   matrix *cmat ;

ENTRY("create_gltsym") ;

   if( sym == NULL || ncol < 1 || nSymStim < 1 ) /* should not happen */
     ERROR_exit("Bad call to create_gltsym!") ;

   if( strncmp(sym,"SYM:",4) == 0 ){  /* read directly from sym string */

     char *fdup=strdup(sym+4) , *fpt , *buf ;
     int ss , ns ;
     buf = fdup ;
STATUS("SYM: gltsym") ;
     while(*buf != '\0'){
                         fpt = strchr(buf,'\\'); /* find end of 'line' */
       if( fpt == NULL ) fpt = strchr(buf,'|') ;
       if( fpt != NULL ) *fpt = '\0' ;           /* mark end of line with NUL */
STATUS(buf) ;
       fvv = SYM_expand_ranges( ncol-1 , nSymStim,SymStim , buf ) ;
       if( fvv == NULL || fvv->nvec < 1 ){ buf=fpt+1; continue; }  /* bad?  blank? */
       far = (float **)realloc((void *)far , sizeof(float *)*(nr+fvv->nvec)) ;
       for( iv=0 ; iv < fvv->nvec ; iv++ ) far[nr++] = fvv->fvar[iv].ar ;
       free((void *)fvv->fvar) ; free((void *)fvv) ;
       if( fpt == NULL ) break ;   /* reached end of string? */
       buf = fpt+1 ;               /* no, so loop back for next 'line' */
     }
     free((void *)fdup) ;

   } else {                             /* read from file */

     char buf[8192] , *cpt ;
     FILE *fp = fopen( sym , "r" ) ;
     if( fp == NULL ) ERROR_exit("Cannot open GLT matrix file '%s'",sym) ;
     while(1){
       cpt = afni_fgets( buf , 8192 , fp ) ;   /* read next line */
       if( cpt == NULL ) break ;               /* end of input? */
       str_echo = THD_zzprintf(str_echo," : %s",cpt) ;
       fvv = SYM_expand_ranges( ncol-1 , nSymStim,SymStim , buf ) ;
       if( fvv == NULL || fvv->nvec < 1 ) continue ;
       far = (float **)realloc((void *)far , sizeof(float *)*(nr+fvv->nvec)) ;
       for( iv=0 ; iv < fvv->nvec ; iv++ ) far[nr++] = fvv->fvar[iv].ar ;
       free((void *)fvv->fvar) ; free((void *)fvv) ;
     }
     fclose(fp) ;

   }

   if( nr == 0 ) ERROR_exit("Cannot read GLT matrix from '%s'",sym) ;
   cmat = (matrix *)malloc(sizeof(matrix)) ; matrix_initialize(cmat) ;
   array_to_matrix( nr , ncol , far , cmat ) ;

   for( ii=0 ; ii < nr ; ii++ ) free((void *)far[ii]) ;
   free((void *)far) ;

   if( !AFNI_noenv("AFNI_GLTSYM_PRINT") ){
     printf("------------------------------------------------------------\n");
     printf("GLT matrix from '%s':\n",sym) ;
     if( str_echo != NULL ){ printf("%s",str_echo); free(str_echo); }
     matrix_print( *cmat ) ;
   }

   /** check for all zero rows, which will cause trouble later **/

   for( ii=0 ; ii < nr ; ii++ ){
     for( jj=0 ; jj < ncol && cmat->elts[ii][jj] == 0.0 ; jj++ ) ; /*nada*/
     if( jj == ncol )
       ERROR_message("Row #%d of GLT matrix '%s' is all zero!", ii+1 , sym ) ;
   }

   RETURN(cmat) ;
}

/*-------------------------------------------------------------------------*/


/*==========================================================================*/
/********************************** Main program ****************************/

int main( int argc , char *argv[] )
{
   THD_3dim_dataset *inset=NULL ;
   MRI_vectim   *inset_mrv=NULL ; /* 05 Nov 2008 */
   int           virtu_mrv=0 ;    /* 13 Dec 2012 */
   char         *fname_mrv=NULL ;
   THD_3dim_dataset *abset=NULL ; int abfixed=0 ; float afix,bfix ;
   MRI_IMAGE *aim=NULL, *bim=NULL ; float *aar=NULL, *bar=NULL ;
   MRI_IMAGE *rim=NULL ;            float *rar=NULL ;
   byte *mask=NULL,*gmask=NULL ;
   bytevec *statmask=NULL ; char *statmask_name=NULL ;     /* 15 Jul 2010 */
   int mask_nx=0,mask_ny=0,mask_nz=0, automask=0, nmask=0;
   float *iv , *jv ; int niv ;
   int iarg, ii,jj,kk, ntime,ddof, *tau=NULL, rnum, nfull, nvals,nvox,vv,rv ;
   NI_element *nelmat=NULL ; char *matname=NULL ;
   double rhomax=0.8 , bmax  =0.8 ; int nlevab=3 ;
   double rm_set=0.0 , bm_set=0.0 ;
   char *cgl , *rst ;
   matrix X ; vector y ;
   reml_collection *rrcol ;
   int nprefixO=0 , nprefixR=0 , vstep=0 ;

   char *Rbeta_prefix =NULL; THD_3dim_dataset *Rbeta_dset =NULL; FILE *Rbeta_fp =NULL; char *Rbeta_fn =NULL;
   char *Rvar_prefix  =NULL; THD_3dim_dataset *Rvar_dset  =NULL; FILE *Rvar_fp  =NULL; char *Rvar_fn  =NULL;
   char *Rfitts_prefix=NULL; THD_3dim_dataset *Rfitts_dset=NULL; FILE *Rfitts_fp=NULL; char *Rfitts_fn=NULL;
   char *Obeta_prefix =NULL; THD_3dim_dataset *Obeta_dset =NULL; FILE *Obeta_fp =NULL; char *Obeta_fn =NULL;
   char *Ovar_prefix  =NULL; THD_3dim_dataset *Ovar_dset  =NULL; FILE *Ovar_fp  =NULL; char *Ovar_fn  =NULL;
   char *Ofitts_prefix=NULL; THD_3dim_dataset *Ofitts_dset=NULL; FILE *Ofitts_fp=NULL; char *Ofitts_fn=NULL;

   char *Rbuckt_prefix=NULL; THD_3dim_dataset *Rbuckt_dset=NULL; FILE *Rbuckt_fp=NULL; char *Rbuckt_fn=NULL;
   char *Obuckt_prefix=NULL; THD_3dim_dataset *Obuckt_dset=NULL; FILE *Obuckt_fp=NULL; char *Obuckt_fn=NULL;
   int nbuckt=0 , do_buckt=0 ;
   int *betaset , nbetaset , nobout=0 ;  /* 25 Mar 2009 */

   char *Rerrts_prefix=NULL; THD_3dim_dataset *Rerrts_dset=NULL; FILE *Rerrts_fp=NULL; char *Rerrts_fn=NULL;
   char *Oerrts_prefix=NULL; THD_3dim_dataset *Oerrts_dset=NULL; FILE *Oerrts_fp=NULL; char *Oerrts_fn=NULL;
   char *Rwherr_prefix=NULL; THD_3dim_dataset *Rwherr_dset=NULL; FILE *Rwherr_fp=NULL; char *Rwherr_fn=NULL;

   char *Rglt_prefix  =NULL; THD_3dim_dataset *Rglt_dset  =NULL; FILE *Rglt_fp  =NULL; char *Rglt_fn  =NULL;
   char *Oglt_prefix  =NULL; THD_3dim_dataset *Oglt_dset  =NULL; FILE *Oglt_fp  =NULL; char *Oglt_fn  =NULL;
   int neglt=0 , do_eglt=0 ;

   int Ngoodlist,*goodlist=NULL , Nruns,*runs=NULL , izero ;
   NI_int_array *giar ; NI_str_array *gsar ; NI_float_array *gfar ;
   float mfilt_radius=0.0 , dx,dy,dz ; int do_mfilt=0 , do_dxyz , nx,ny,nz,nxy ;
   int do_Ostuff=0 , do_Rstuff=0 ;

   int glt_num=0, glt_rtot=0 , do_glt=1 ; matrix **glt_mat=NULL ; char **glt_lab=NULL ;
   sparmat **glt_smat=NULL ;
   GLT_index **glt_ind=NULL ;
   int stim_num=0; int *stim_bot=NULL , *stim_top=NULL ; char **stim_lab=NULL ;
   int do_fstat=0 , do_tstat=0 , do_stat=0 , do_rstat=0 ;
   int num_allstim=0, *allstim=NULL , num_basetim=0, *basetim=NULL ;

   char **beta_lab=NULL ;
   int do_FDR = 1 ;

   int usetemp_rcol=0 ;

   MRI_IMARR *imar_addbase=NULL ; int ncol_addbase=0 ;
   MRI_IMARR *imar_slibase=NULL ; int ncol_slibase=0 , nfile_slibase=0 ;
   int nrega,nrego , nbad , ss,ssold , dmbase=1 , nregda , nregu ;
   int               nsli , nsliper ;
   matrix          **Xsli =NULL ;
   reml_collection **RCsli=NULL ;

   int                   num_dsort = 0 ;
   THD_3dim_dataset_array *dsortar = NULL ;  /* 22 Jul 2015 */
   reml_setup_plus *rsetp_dsort    = NULL ;
   MRI_vectim  **dsortar_mrv       = NULL ;
   float       **dsortar_mean      = NULL ;
   matrix *dsort_Zmat              = NULL ;
   float *dsort_iv=NULL , *dsort_jv= NULL ;
   int                nconst_dsort = 0 ;
   int                dsort_nods   = 0 ;     /* 27 Jul 2015 */
   int                doing_nods   = 0 ;
   int              last_nods_trip = 0 ;
   char         *dsort_nods_suffix = "_nods" ;

   reml_setup *my_rset  = NULL ;
   matrix     *my_Xmat  = NULL ;
   sparmat    *my_Xsmat = NULL ;
   int         my_kill  = 0 ;

   int    eglt_num = 0    ;   /* extra GLTs from the command line */
   char **eglt_lab = NULL ;
   char **eglt_sym = NULL ;
   int    oglt_num = 0    ;   /* number of 'original' GLTs */

   int polort = 0 ;           /* 11 Mar 2010 */
   MRI_IMAGE *matim = NULL ;

   int nallz = 0 ; int *allz = NULL ;  /* 15 Mar 2010 */

   int LJ_hh = 0 ; /* Ljung-Box lag parameter [21 Jan 2020] */
   int min_run = 0 , max_run = 0 ; /* shortest and longest runs */

   /**------------ Get by with a little help from your friends? ------------**/

   set_obliquity_report(0); /* silence obliquity */

   Argc = argc ; Argv = argv ;

   AFNI_SETUP_OMP(0) ;  /* 24 Jun 2013 */

   if( argc < 2 || strcasecmp(argv[1],"-help") == 0 ){
     printf(
      "Usage: 3dREMLfit [options]  ~1~\n"
      "\n"
      "   **** Generalized least squares time series fit, with REML   ****\n"
      "   **** estimation of the temporal auto-correlation structure. ****\n"
      "\n"
      "---------------------------------------------------------------------\n"
      "****  The recommended way to use 3dREMLfit is via afni_proc.py,  ****\n"
      "****  which will pre-process the data, and also give some useful ****\n"
      "****  diagnostic tools/outputs for assessing the data's quality. ****\n"
      "****  [afni_proc.py will make your FMRI-analysis life happier!]  ****\n"
      "---------------------------------------------------------------------\n"
      "\n"
      "* This program provides a generalization of 3dDeconvolve:\n"
      "    it allows for serial correlation in the time series noise.\n"
      "\n"
      "* It solves the linear equations for each voxel in the generalized\n"
      "    (prewhitened) least squares sense, using the REML estimation method\n"
      "    to find a best-fit ARMA(1,1) model for the time series noise\n"
      "    correlation matrix in each voxel (i.e., each voxel gets a separate\n"
      "    pair of ARMA parameters).\n"
      "  ++ Note that the 2-parameter ARMA(1,1) correlation model is hard-coded\n"
      "     into this program -- there is no way to use a more elaborate model,\n"
      "     such as the 5-parameter ARMA(3,2), in 3dREMLfit.\n"
      "  ++ A 'real' REML optimization of the autocorrelation model is made,\n"
      "     not simply an adjustment based on the residuals from a preliminary\n"
      "     OLSQ regression.\n"
      "  ++ See the section 'What is ARMA(1,1)' (far below) for more fun details.\n"
      "  ++ And the section 'What is REML' (even farther below).\n"
      "\n"
      "* You MUST run 3dDeconvolve first to generate the input matrix\n"
      "    (.xmat.1D) file, which contains the hemodynamic regression\n"
      "    model, censoring and catenation information, the GLTs, etc.\n"
      "    See the output of '3dDeconvolve -help' for information on\n"
      "    using that program to setup the analysis.\n"
      "  ++ However, you can input a 'naked' (non-3dDeconvolve) matrix\n"
      "     file using the '-matim' option, if you know what you are doing.\n"
      "\n"
      "* If you don't want the 3dDeconvolve analysis to run, you can\n"
      "    prevent that by using 3dDeconvolve's '-x1D_stop' option.\n"
      "\n"
      "* 3dDeconvolve also prints out a cognate command line for running\n"
      "    3dREMLfit, which should get you going with relative ease.\n"
      "\n"
      "* The output datasets from 3dREMLfit are structured to resemble\n"
      "    the corresponding results from 3dDeconvolve, to make it\n"
      "    easy to adapt your scripts for further processing.\n"
      "\n"
      "* Is this type of analysis (generalized least squares) important?\n"
      "    That depends on your point of view, your data, and your goals.\n"
      "    If you really want to know the answer, you should run\n"
      "    your analyses both ways (with 3dDeconvolve and 3dREMLfit),\n"
      "    through to the final step (e.g., group analysis), and then\n"
      "    decide if your neuroscience/brain conclusions depend strongly\n"
      "    on the type of linear regression that was used.\n"
      "\n"
      "* If you are planning to use 3dMEMA for group analysis, then using\n"
      "    3dREMLfit instead of 3dDeconvolve is a good idea. 3dMEMA uses\n"
      "    the t-statistic of the beta weight as well as the beta weight\n"
      "    itself -- and the t-values from 3dREMLfit are probably more\n"
      "    more accurate than those from 3dDeconvolve, since the underlying\n"
      "    variance estimate should be more accurate (less biased).\n"
      "\n"
      "* When there is significant temporal correlation, and you are using\n"
      "    'IM' regression (estimated individual betas for each event),\n"
      "    the REML GLSQ regression can be superior to OLSQ beta\n"
      "    estimates -- in the sense that the resulting betas\n"
      "    have somewhat less variance with GLSQ than with OLSQ.\n"
      "\n"
      "-------------------------------------------\n"
      "Input Options (the first two are mandatory)  ~1~\n"
      "-------------------------------------------\n"
      " -input ddd  = Read time series dataset 'ddd'.\n"
      "              * This is the dataset without censoring!\n"
      "              * The '-matrix' file, on the other hand, encodes\n"
      "                 which time points are to be censored, and the\n"
      "                 matrix stored therein is already censored.\n"
      "              * The doc below has a discussion of censoring in 3dREMLfit:\n"
      "   https://afni.nimh.nih.gov/pub/dist/doc/htmldoc/statistics/remlfit.html\n"
      "\n"
      " -matrix mmm = Read the matrix 'mmm', which should have been\n"
      "                 output from 3dDeconvolve via the '-x1D' option.\n"
      "\n"
      "             ** N.B.: You can omit entirely defining the regression matrix,\n"
      "                       but then the program will fabricate a matrix consisting\n"
      "                       of a single column with all 1s. This option is\n"
      "                       mostly for the convenience of the author; for\n"
      "                       example, to have some fun with an AR(1) time series:\n"
      "                 1deval -num 1001 -expr 'gran(0,1)+(i-i)+0.7*z' > g07.1D\n"
      "                 3dREMLfit -input g07.1D'{1..$}'\' -Rvar -.1D -grid 5 -MAXa 0.9\n"
      "\n"
      "             ** N.B.: 3dREMLfit now supports all zero columns, if you use\n"
      "                        the '-GOFORIT' option. [Ides of March, MMX A.D.]\n"
     ) ;

     printf(
      "\n"
      " More Primitive Alternative Ways to Define the Regression Matrix\n"
      " --------------------------------------------------------------------------\n"
      " -polort P = If no -matrix option is given, AND no -matim option,\n"
      "               create a matrix with Legendre polynomial regressors\n"
      "               up to order 'P'. The default value is P=0, which\n"
      "               produces a matrix with a single column of all ones.\n"
      "               (That is the default matrix described above.)\n"
      "\n"
      " -matim M  = Read a standard .1D file as the matrix.\n"
      "            * That is, an ASCII files of numbers layed out in a\n"
      "              rectangular array. The number of rows must equal the\n"
      "              number of time points in the input dataset. The number\n"
      "              of columns is the number of regressors.\n"
      "            * Advanced features, such as censoring, can only be implemented\n"
      "              by providing a true .xmat.1D file via the '-matrix' option.\n"
      "           ** However, censoring can still be applied (in a way) by including\n"
      "              extra columns in the matrix. For example, to censor out time\n"
      "              point #47, a column that is 1 at time point #47 and zero at\n"
      "              all other time points can be used.\n"
      "             ++ Remember that AFNI counting starts at 0, so this column\n"
      "                would start with 47 0s, then a single 1, then the rest\n"
      "                of the entries would be 0s.\n"
      "             ++ 3dDeconvolve option '-x1D_regcensored' will create such a\n"
      "                .xmat.1D file, with the censoring indicated by 0-1 columns\n"
      "                rather than by the combination of 'GoodList' and omitted\n"
      "                rows. That is, instead of shrinking the matrix (by rows)\n"
      "                it will expand the matrix (by columns).\n"
      "             ++ You can strip out the XML-ish header from the .xmat.1D\n"
      "                file with a Unix command like this:\n"
      "                  grep -v '^#' Fred.xmat.1D > Fred.rawmat.1D\n"
      "             ++ In cases with lots of censoring, expanding the matrix\n"
      "                by lots of columns will make 3dREMLfit run more slowly.\n"
      "                For most situations, this slowdown will not be horrific.\n"
      "            * An advanced intelligence could reverse engineer the XML\n"
      "              format used by the .xmat.1D files, to fully activate all the\n"
      "              regression features of this software :)\n"
      "           ** N.B.: You can use only 'Col' as a name in GLTs ('-gltsym')\n"
      "                    with these nonstandard matrix input methods, since\n"
      "                    the other column names come from the '-matrix' file.\n"
      " ** These mutually exclusive options are ignored if -matrix is used.\n"
      " ----------------------------------------------------------------------------\n"
      " The matrix supplied is the censored matrix, if any time points are marked\n"
      " as to be removed from the analysis -- that is, if GoodList (infra) is NOT\n"
      " the entire list of time points from 0..(nrow-1).\n"
      "\n"
      " Information supplied in the .xmat.1D format XML header's attributes\n"
      " includes the following (but is not limited to):\n"
      "  * ColumnLabels = a string label for each column in the matrix\n"
      "  * ColumnGroups = groupings of columns into associated regressors\n"
      "                   (e.g., motion, baseline, task)\n"
      "  * RowTR        = TR in seconds\n"
      "  * GoodList     = list of time points to use (inverse of censor list)\n"
      "  * NRowFull     = size of full matrix (without censoring)\n"
      "  * RunStart     = time point indexes of start of the runs\n"
      "  * Nstim        = number of distinct stimuli\n"
      "  * StimBots     = column indexes for beginning of each stimulus's regressors\n"
      "  * StimTops     = column indexes for ending of each stimulus's regressors\n"
      "  * StimLabels   = names for each stimulus\n"
      "  * CommandLine  = string of command used to create the file\n"
      "See the doc below for a lengthier description of the matrix format:\n"
      "  https://afni.nimh.nih.gov/pub/dist/doc/htmldoc/statistics/remlfit.html\n"
      " ----------------------------------------------------------------------------\n"
     ) ;

     printf(
      "\n"
      "---------------\n"
      "Masking options  ~1~\n"
      "---------------\n"
      " -mask MMM   = Read dataset 'MMM' as a mask for the input; voxels outside\n"
      "                 the mask will not be fit by the regression model.\n"
      " -automask   = If you don't know what this does by now, I'm not telling.\n"
      "            *** If you don't specify ANY mask, the program will\n"
      "                 build one automatically (from each voxel's RMS)\n"
      "                 and use this mask SOLELY for the purpose of\n"
      "                 computing the FDR curves in the bucket dataset's header.\n"
      "              * If you DON'T want this to happen, then use '-noFDR'\n"
      "                 and later run '3drefit -addFDR' on the bucket dataset.\n"
      "              * To be precise, the FDR automask is only built if\n"
      "                 the input dataset has at least 5 voxels along each of\n"
      "                 the x and y axes, to avoid applying it when you run\n"
      "                 3dREMLfit on 1D timeseries inputs.\n"
      "-STATmask ss = Build a mask from file 'ss', and use this for the purpose\n"
      "                 of computing the FDR curves.\n"
      "              * The actual results ARE not masked with this option\n"
      "                  (only with '-mask' or '-automask' options).\n"
      "              * If you don't use '-STATmask', then the mask from\n"
      "                  '-mask' or '-automask' is used for the FDR work.\n"
      "                  If neither of those is given, then the automatically\n"
      "                  generated mask described just above is used for FDR.\n"
      "\n"
      "--------------------------------------------------------------------------\n"
      "Options to Add Baseline (Null Hypothesis) Columns to the Regression Matrix  ~1~\n"
      "--------------------------------------------------------------------------\n"
      " -addbase bb = You can add baseline model columns to the matrix with\n"
      "                 this option. Each column in the .1D file 'bb' will\n"
      "                 be appended to the matrix. This file must have at\n"
      "                 least as many rows as the matrix does.\n"
      "              * Multiple -addbase options can be used, if needed.\n"
      "              * More than 1 file can be specified, as in\n"
      "                  -addbase fred.1D ethel.1D elvis.1D\n"
      "              * None of the .1D filename can start with the '-' character,\n"
      "                  since that is the signal for the next option.\n"
      "              * If the matrix from 3dDeconvolve was censored, then\n"
      "                  this file (and '-slibase' files) can either be\n"
      "                  censored to match, OR 3dREMLfit will censor these\n"
      "                  .1D files for you.\n"
      "               + If the column length (number of rows) of the .1D file\n"
      "                   is the same as the column length of the censored\n"
      "                   matrix, then the .1D file WILL NOT be censored.\n"
      "               + If the column length of the .1D file is the same\n"
      "                   as the column length of the uncensored matrix,\n"
      "                   then the .1D file WILL be censored -- the same\n"
      "                   rows excised from the matrix in 3dDeconvolve will\n"
      "                   be resected from the .1D file before the .1D file's\n"
      "                   columns are appended to the matrix.\n"
      "               + The censoring information from 3dDeconvolve is stored\n"
      "                   in the matrix file header, and you don't have to\n"
      "                   provide it again on the 3dREMLfit command line.\n"
      "\n"
      " -dsort dset = Similar to -addbase in concept, BUT the dataset 'dset'\n"
      "                 provides a different baseline regressor for every\n"
      "                 voxel. This dataset must have the same number of\n"
      "                 time points as the input dataset, and have the same\n"
      "                 number of voxels.                          [Added 22 Jul 2015]\n"
      "               + The REML (a,b) estimation is done WITHOUT this extra\n"
      "                   voxel-wise regressor, and then the selected (a,b)\n"
      "                   ARMA parameters are used to do the final regression for\n"
      "                   the '-R...' output datasets. This method is not ideal,\n"
      "                   but the alternative of re-doing the (a,b) estimation with\n"
      "                   a different matrix for each voxel would be VERY slow.\n"
      "                   -- The -dsort estimation is thus different from the -addbase\n"
      "                      and/or -slibase estimations, in that the latter cases\n"
      "                      incorporate the extra regressors into the REML estimation\n"
      "                      of the ARMA (a,b) parameters. The practical difference\n"
      "                      between these two methods is usually very small ;-)\n"
      "               + If any voxel time series from -dsort is constant through time,\n"
      "                   the program will print a warning message, and peculiar things\n"
      "                   might happen. Gleeble, fitzwilly, blorten, et cetera.\n"
      "                   -- Actually, if this event happens, the 'offending' -dsort voxel\n"
      "                      time series is replaced by the mean time series from that\n"
      "                      -dsort dataset.\n"
      "               + The '-Rbeta' (and/or '-Obeta') option will include the\n"
      "                   fit coefficient for the -dsort regressor (last).\n"
      "               + There is no way to include the -dsort regressor beta in a GLT.\n"
      "               + You can use -dsort more than once. Please don't go crazy.\n"
      "               + Using this option slows the program down in the GLSQ loop,\n"
      "                   since a new matrix and GLT set must be built up and torn down\n"
      "                   for each voxel separately.\n"
#ifdef USE_OMP
      "              -- At this time, the GLSQ loop is not OpenMP-ized.\n"
#endif
      "             +++ This voxel-wise regression capability is NOT implemented in\n"
      "                   3dDeconvolve, so you'll have to use 3dREMLfit if you want\n"
      "                   to use this method, even if you only want ordinary least\n"
      "                   squares regression.\n"
      "               + The motivation for -dsort is to apply ANATICOR to task-based\n"
      "                   FMRI analyses. You might be clever and have a better idea!?\n"
      "                  http://www.ncbi.nlm.nih.gov/pmc/articles/PMC2897154/\n"
      "                  https://afni.nimh.nih.gov/pub/dist/doc/program_help/afni_proc.py.html\n"
      "\n"
      " -dsort_nods = If '-dsort' is used, the output datasets reflect the impact of the\n"
      "                 voxel-wise regressor(s). If you want to compare those results\n"
      "                 to the case where you did NOT give the '-dsort' option, then\n"
      "                 also use '-dsort_nods' (nods is short for 'no dsort').\n"
      "                 The linear regressions will be repeated without the -dsort\n"
      "                 regressor(s) and the results put into datasets with the string\n"
      "                 '_nods' added to the prefix.\n"
      "\n"
      " -slibase bb = Similar to -addbase in concept, BUT each .1D file 'bb'\n"
      "                 must have an integer multiple of the number of slices\n"
      "                 in the input dataset; then, separate regression\n"
      "                 matrices are generated for each slice, with the\n"
      "                 [0] column of 'bb' appended to the matrix for\n"
      "                 the #0 slice of the dataset, the [1] column of 'bb'\n"
      "                 appended to the matrix for the #1 slice of the dataset,\n"
      "                 and so on. For example, if the dataset has 3 slices\n"
      "                 and file 'bb' has 6 columns, then the order of use is\n"
      "                     bb[0] --> slice #0 matrix\n"
      "                     bb[1] --> slice #1 matrix\n"
      "                     bb[2] --> slice #2 matrix\n"
      "                     bb[3] --> slice #0 matrix\n"
      "                     bb[4] --> slice #1 matrix\n"
      "                     bb[5] --> slice #2 matrix\n"
      "             ** If this order is not correct, consider -slibase_sm.\n"
      "              * Intended to help model physiological noise in FMRI,\n"
      "                 or other effects you want to regress out that might\n"
      "                 change significantly in the inter-slice time intervals.\n"
      "              * Slices are the 3rd dimension in the dataset storage\n"
      "                 order -- 3dinfo can tell you what that direction is:\n"
      "                   Data Axes Orientation:\n"
      "                     first  (x) = Right-to-Left\n"
      "                     second (y) = Anterior-to-Posterior\n"
      "                     third  (z) = Inferior-to-Superior   [-orient RAI]\n"
      "                 In the above example, the slice direction is from\n"
      "                 Inferior to Superior, so the columns in the '-slibase'\n"
      "                 input file should be ordered in that direction as well.\n"
      "              * '-slibase' will slow the program down, and make it use\n"
      "                  a lot more memory (to hold all the matrix stuff).\n"
      "            *** At this time, 3dSynthesize has no way of incorporating the\n"
      "                  extra baseline timeseries from -addbase or -slibase or -dsort.\n"
      "            *** Also see option '-dsort' for how to include voxel-dependent\n"
      "                regressors into the analysis.\n"
      "\n"
      " -slibase_sm bb = Similar to -slibase above, BUT each .1D file 'bb'\n"
      "                 must be in slice major order (i.e. all slice0 columns\n"
      "                 come first, then all slice1 columns, etc).\n"
      "                 For example, if the dataset has 3 slices and file\n"
      "                 'bb' has 6 columns, then the order of use is\n"
      "                     bb[0] --> slice #0 matrix, regressor 0\n"
      "                     bb[1] --> slice #0 matrix, regressor 1\n"
      "                     bb[2] --> slice #1 matrix, regressor 0\n"
      "                     bb[3] --> slice #1 matrix, regressor 1\n"
      "                     bb[4] --> slice #2 matrix, regressor 0\n"
      "                     bb[5] --> slice #2 matrix, regressor 1\n"
      "             ** If this order is not correct, consider -slibase.\n"
      "\n"
      " -usetemp    = Write intermediate stuff to disk, to economize on RAM.\n"
      "                 Using this option might be necessary to run with\n"
      "                 '-slibase' and with '-Grid' values above the default,\n"
      "                 since the program has to store a large number of\n"
      "                 matrices for such a problem: two for every slice and\n"
      "                 for every (a,b) pair in the ARMA parameter grid.\n"
      "              * '-usetemp' can actually speed the program up, interestingly,\n"
      "                   even if you have enough RAM to hold all the intermediate\n"
      "                   matrices needed with '-slibase'. YMMV :)\n"
      "              * '-usetemp' also writes temporary files to store dataset\n"
      "                   results, which can help if you are creating multiple large\n"
      "                   dataset (e.g., -Rfitts and -Rerrts in the same program run).\n"
      "              * Temporary files are written to the directory given\n"
      "                  in environment variable TMPDIR, or in /tmp, or in ./\n"
      "                  (preference is in that order).\n"
      "                 + If the program crashes, these files are named\n"
      "                     REML_somethingrandom, and you might have to\n"
      "                     delete them manually.\n"
      "                 + If the program ends normally, it will delete\n"
      "                     these temporary files before it exits.\n"
      "                 + Several gigabytes of disk space might be used\n"
      "                     for this temporary storage!\n"
      "                 + When running on a cluster, or some other system\n"
      "                     using networked storage, '-usetemp' will work\n"
      "                     MUCH better if the temporary storage directory\n"
      "                     is a local drive rather than a networked drive.\n"
      "                     You will have to figure out how to do this on\n"
      "                     your cluster, since configurations vary so much.\n"
      "                   * If you are at the NIH, then see this Web page:\n"
      "                       https://hpc.nih.gov/docs/userguide.html#local\n"
      "              * If the program crashes with a 'malloc failure' type of\n"
      "                  message, then try '-usetemp' (malloc=memory allocator).\n"
      "            *** NOTE THIS: If a Unix program stops suddenly with the\n"
      "                           mysterious one word message 'killed', then it\n"
      "                           almost certainly ran over some computer system\n"
      "                           limitations, and was immediately stopped without\n"
      "                           any recourse. Usually the resource it ran out\n"
      "                           of is memory. So if this happens to you when\n"
      "                           running 3dREMLfit, try using the '-usetemp' option!\n"
#ifdef USING_MCW_MALLOC
      "              * If you use '-verb', then memory usage is printed out\n"
      "                  at various points along the way.\n"
#endif
#ifdef USE_OMP
      "              * '-usetemp' disables OpenMP multi-CPU usage.\n"
      "                  Only use this option if you need to, since OpenMP should\n"
      "                  speed the program up significantly on multi-CPU computers.\n"
#endif
      "\n"
      " -nodmbase   = By default, baseline columns added to the matrix\n"
      "                 via '-addbase' or '-slibase' or '-dsort' will each have\n"
      "                 their mean removed (as is done in 3dDeconvolve). If you\n"
      "                 do NOT want this operation performed, use '-nodmbase'.\n"
      "              * Using '-nodmbase' would make sense if you used\n"
      "                 '-polort -1' to set up the matrix in 3dDeconvolve, and/or\n"
      "                 you actually care about the fit coefficients of the extra\n"
      "                 baseline columns (in which case, don't use '-nobout').\n"
      "\n"
      "------------------------------------------------------------------------\n"
      "Output Options (at least one must be given; 'ppp' = dataset prefix name)  ~1~\n"
      "------------------------------------------------------------------------\n"
      " -Rvar  ppp  = dataset for saving REML variance parameters\n"
      "               * See the 'What is ARMA(1,1)' section, far below.\n"
      "               * This dataset has 6 volumes:\n"
      "                [0] = 'a'       = ARMA parameter\n"
      "                                = decay rate of correlations with lag\n"
      "                [1] = 'b'       = ARMA parameter\n"
      "                [2] = 'lam'     = (b+a)(1+a*b)/(1+2*a*b+b*b)\n"
      "                                = correlation at lag=1\n"
      "                                  correlation at lag=k is lam * a^(k-1) (k>0)\n"
      "                [3] = 'StDev'   = standard deviation of prewhitened\n"
      "                                  residuals (used in computing statistics\n"
      "                                  in '-Rbuck' and in GLTs)\n"
      "                [4] = '-LogLik' = negative of the REML log-likelihood\n"
      "                                  function (see the math notes)\n"
      "                [5] = 'LjungBox'= Ljung-Box statistic of the pre-whitened\n"
      "                                  residuals, an indication of how much\n"
      "                                  temporal correlation is left-over.\n"
      "                                + See the 'Other Commentary' section far below\n"
      "                                  for a little more information on the LB\n"
      "                                  statistic.\n"
      "               * The main purpose of this dataset is to check when weird\n"
      "                   things happen in the calculations. Or just to have fun.\n"
      " -Rbeta ppp  = dataset for beta weights from the REML estimation\n"
      "                 [similar to the -cbucket output from 3dDeconvolve]\n"
      "               * This dataset will contain all the beta weights, for\n"
      "                   baseline and stimulus regressors alike, unless the\n"
      "                   '-nobout' option is given -- in that case, this\n"
      "                   dataset will only get the betas for the stimulus\n"
      "                   regressors.\n"
      " -Rbuck ppp  = dataset for beta + statistics from the REML estimation;\n"
      "                 also contains the results of any GLT analysis requested\n"
      "                 in the 3dDeconvolve setup.\n"
      "                 [similar to the -bucket output from 3dDeconvolve]\n"
      "               * This dataset does NOT get the betas (or statistics) of\n"
      "                   those regressors marked as 'baseline' in the matrix file.\n"
      "               * If the matrix file from 3dDeconvolve does not contain\n"
      "                   'Stim attributes' (which will happen if all inputs\n"
      "                   to 3dDeconvolve were labeled as '-stim_base'), then\n"
      "                   -Rbuck won't work, since it is designed to give the\n"
      "                   statistics for the 'stimuli' and there aren't any matrix\n"
      "                   columns labeled as being 'stimuli'.\n"
      "               * In such a case, to get statistics on the coefficients,\n"
      "                   you'll have to use '-gltsym' and '-Rglt'; for example,\n"
      "                   to get t-statistics for all coefficients from #0 to #77:\n"
      "                      -tout -Rglt Colstats -gltsym 'SYM: Col[[0..77]]' ColGLT\n"
      "                 where 'Col[3]' is the generic label that refers to matrix\n"
      "                 column #3, et cetera.\n"
      "               * FDR curves for so many statistics (78 in the example)\n"
      "                   might take a long time to generate!\n"
      " -Rglt  ppp  = dataset for beta + statistics from the REML estimation,\n"
      "                 but ONLY for the GLTs added on the 3dREMLfit command\n"
      "                 line itself via '-gltsym'; GLTs from 3dDeconvolve's\n"
      "                 command line will NOT be included.\n"
      "               * Intended to give an easy way to get extra contrasts\n"
      "                   after an earlier 3dREMLfit run.\n"
      "               * Use with '-ABfile vvv' to read the (a,b) parameters\n"
      "                   from the earlier run, where 'vvv' is the '-Rvar'\n"
      "                   dataset output from that run.\n"
      "                   [If you didn't save the '-Rvar' file, then it will]\n"
      "                   [be necessary to redo the REML loop, which is slow]\n"
      "\n"
      " -fout       = put F-statistics into the bucket dataset\n"
      " -rout       = put R^2 statistics into the bucket dataset\n"
      " -tout       = put t-statistics into the bucket dataset\n"
      "                 [if you use -Rbuck and do not give any of -fout, -tout,]\n"
      "                 [or -rout, then the program assumes -fout is activated.]\n"
      " -noFDR      = do NOT add FDR curve data to bucket datasets\n"
      "                 [FDR curves can take a long time if -tout is used]\n"
      "\n"
      " -nobout     = do NOT add baseline (null hypothesis) regressor betas\n"
      "                 to the -Rbeta and/or -Obeta output datasets.\n"
      "                 ['stimulus' columns are marked in the .xmat.1D matrix ]\n"
      "                 [file; all other matrix columns are 'baseline' columns]\n"
      "\n"
      " -Rfitts ppp = dataset for REML fitted model\n"
      "                 [like 3dDeconvolve, a censored time point gets]\n"
      "                 [the actual data values from that time index!!]\n"
      "\n"
      " -Rerrts ppp = dataset for REML residuals = data - fitted model\n"
      "                 [like 3dDeconvolve,  a censored time]\n"
      "                 [point gets its residual set to zero]\n"
      " -Rwherr ppp = dataset for REML residual, whitened using the\n"
      "                 estimated ARMA(1,1) correlation matrix of the noise\n"
      "                 [Note that the whitening matrix used is the inverse  ]\n"
      "                 [of the Choleski factor of the correlation matrix C; ]\n"
      "                 [however, the whitening matrix isn't uniquely defined]\n"
      "                 [(any matrix W with C=inv(W'W) will work), so other  ]\n"
      "                 [whitening schemes could be used and these would give]\n"
      "                 [different whitened residual time series datasets.   ]\n"
      "\n"
      " -gltsym g h = read a symbolic GLT from file 'g' and label it with\n"
      "                 string 'h'\n"
      "                * As in 3dDeconvolve, you can also use the 'SYM:' method\n"
      "                    to put the definition of the GLT directly on the\n"
      "                    command line.\n"
      "                * The symbolic labels for the stimuli are as provided\n"
      "                    in the matrix file, from 3dDeconvolve.\n"
      "              *** Unlike 3dDeconvolve, you supply the label 'h' for\n"
      "                    the output coefficients and statistics directly\n"
      "                    after the matrix specification 'g'.\n"
      "                * Like 3dDeconvolve, the matrix generated by the\n"
      "                    symbolic expression will be printed to the screen\n"
      "                    unless environment variable AFNI_GLTSYM_PRINT is NO.\n"
      "                * These GLTs are in addition to those stored in the\n"
      "                    matrix file, from 3dDeconvolve.\n"
      "                * If you don't create a bucket dataset using one of\n"
      "                    -Rbuck or -Rglt (or -Obuck / -Oglt), using\n"
      "                    -gltsym is completely pointless and stupid!\n"
      "               ** Besides the stimulus labels read from the matrix\n"
      "                    file (put there by 3dDeconvolve), you can refer\n"
      "                    to regressor columns in the matrix using the\n"
      "                    symbolic name 'Col', which collectively means\n"
      "                    all the columns in the matrix. 'Col' is a way\n"
      "                    to test '-addbase' and/or '-slibase' regressors\n"
      "                    for significance; for example, if you have a\n"
      "                    matrix with 10 columns from 3dDeconvolve and\n"
      "                    add 2 extra columns to it, then you could use\n"
      "                      -gltsym 'SYM: Col[[10..11]]' Addons -tout -fout\n"
      "                    to create a GLT to include both of the added\n"
      "                    columns (numbers 10 and 11).\n"
      "                    -- 'Col' cannot be used to test the '-dsort'\n"
      "                       regressor for significance!\n"
      "\n"
      "The options below let you get the Ordinary Least SQuares outputs\n"
      "(without adjustment for serial correlation), for comparisons.\n"
      "These datasets should be essentially identical to the results\n"
      "you would get by running 3dDeconvolve (with the '-float' option!):\n"
      "\n"
      " -Ovar   ppp = dataset for OLSQ st.dev. parameter (kind of boring)\n"
      " -Obeta  ppp = dataset for beta weights from the OLSQ estimation\n"
      " -Obuck  ppp = dataset for beta + statistics from the OLSQ estimation\n"
      " -Oglt   ppp = dataset for beta + statistics from '-gltsym' options\n"
      " -Ofitts ppp = dataset for OLSQ fitted model\n"
      " -Oerrts ppp = dataset for OLSQ residuals (data - fitted model)\n"
      "                 [there is no -Owherr option; if you don't]\n"
      "                 [see why, then think about it for a while]\n"
      "\n"
      "Note that you don't have to use any of the '-R' options; you could\n"
      "use 3dREMLfit just for the '-O' options if you want. In that case,\n"
      "the program will skip the time consuming ARMA(1,1) estimation for\n"
      "each voxel, by pretending you used the option '-ABfile =0,0'.\n"
      "\n"
      "-------------------------------------------------------------------\n"
      "The following options control the ARMA(1,1) parameter estimation     ~1~\n"
      "for each voxel time series; normally, you do not need these options\n"
      "-------------------------------------------------------------------\n"
      " -MAXa am   = Set max allowed AR a parameter to 'am' (default=0.8).\n"
      "                The range of a values scanned is   0 .. +am (-POScor)\n"
      "                                           or is -am .. +am (-NEGcor).\n"
      "\n"
      " -MAXb bm   = Set max allow MA b parameter to 'bm' (default=0.8).\n"
      "                The range of b values scanned is -bm .. +bm.\n"
      "               * The largest value allowed for am and bm is 0.9.\n"
      "               * The smallest value allowed for am and bm is 0.1.\n"
      "               * For a nearly pure AR(1) model, use '-MAXb 0.1'\n"
      "               * For a nearly pure MA(1) model, use '-MAXa 0.1'\n"
      "\n"
      " -Grid pp   = Set the number of grid divisions in the (a,b) grid\n"
      "                to be 2^pp in each direction over the range 0..MAX.\n"
      "                The default (and minimum) value for 'pp' is 3.\n"
      "                Larger values will provide a finer resolution\n"
      "                in a and b, but at the cost of some CPU time.\n"
      "               * To be clear, the default settings use a grid\n"
      "                   with 8 divisions in the a direction and 16 in\n"
      "                   the b direction (since a is non-negative but\n"
      "                   b can be either sign).\n"
      "               * If -NEGcor is used, then '-Grid 3' means 16 divisions\n"
      "                   in each direction, so that the grid spacing is 0.1\n"
      "                   if MAX=0.8. Similarly, '-Grid 4' means 32 divisions\n"
      "                   in each direction, '-Grid 5' means 64 divisions, etc.\n"
      "               * I see no reason why you would ever use a -Grid size\n"
      "                   greater than 5 (==> parameter resolution = 0.025).\n"
      "                 ++ However, if you like burning up CPU time, values up\n"
      "                    to '-Grid 7' are allowed :)\n"
      "               * In the future, '-Grid 5' might become the default, since\n"
      "                   it is a little more accurate and computers are a lot\n"
      "                   faster than in the days when I was hunting brontosauri.\n"
      "               * In my limited experiments, there was little appreciable\n"
      "                   difference in activation maps between '-Grid 3' and\n"
      "                   '-Grid 5', especially at the group analysis level.\n"
      "                 ++ To be fair, skipping prewhitening by using OLSQ\n"
      "                    (e.g., 3dDeconvolve) at the single subject level\n"
      "                    has little effect on the group analysis UNLESS you\n"
      "                    are going to use 3dMEMA, which relies on accurate\n"
      "                    single subject t-statistics, which in turn requires\n"
      "                    accurate temporal autocorrelation modeling.\n"
      "                 ++ If you are interested in the REML parameters themselves,\n"
      "                    or in getting the 'best' prewhitening possible, then\n"
      "                    '-Grid 5' makes sense.\n"
      "               * The program is somewhat slower as the -Grid size expands.\n"
      "                   And uses more memory, to hold various matrices for\n"
      "                   each (a,b) case.\n"
      "\n"
      " -NEGcor    = Allows negative correlations to be used; the default\n"
      "                is that only positive correlations are searched.\n"
      "                When this option is used, the range of a scanned\n"
      "                is -am .. +am; otherwise, it is 0 .. +am.\n"
      "               * Note that when -NEGcor is used, the number of grid\n"
      "                   points in the a direction doubles to cover the\n"
      "                   range -am .. 0; this will slow the program down.\n"
      " -POScor    = Do not allow negative correlations. Since this is\n"
      "                the default, you don't actually need this option.\n"
      "                [FMRI data doesn't seem to need the modeling  ]\n"
      "                [of negative correlations, but you never know.]\n"
      " -WNplus    = Do not allow negative correlations, AND only allow\n"
      "                (a,b) parameter combinations that fit the model\n"
      "                AR(1) + white noise:\n"
      "               * a > 0  and  -a < b < 0\n"
      "               * see 'What is ARMA(1,1)' far below\n"
      "               * you should use '-Grid 5' with this option, since\n"
      "                 it restricts the number of possible ARMA(1,1) models\n"
      "\n"
      " -Mfilt mr  = After finding the best fit parameters for each voxel\n"
      "                in the mask, do a 3D median filter to smooth these\n"
      "                parameters over a ball with radius 'mr' mm, and then\n"
      "                use THOSE parameters to compute the final output.\n"
      "               * If mr < 0, -mr is the ball radius in voxels,\n"
      "                   instead of millimeters.\n"
      "                 [No median filtering is done unless -Mfilt is used.]\n"
      "               * This option is not recommended; it is just here for\n"
      "                 experimentation.\n"
      "\n"
      " -CORcut cc = The exact ARMA(1,1) correlation matrix (for a != 0)\n"
      "                has no non-zero entries. The calculations in this\n"
      "                program set correlations below a cutoff to zero.\n"
      "                The default cutoff is %.5f, but can be altered with\n"
      "                this option. The usual reason to use this option is\n"
      "                to test the sensitivity of the results to the cutoff.\n"
      "\n"
      " -ABfile ff = Instead of estimating the ARMA(a,b) parameters from the\n"
      "                data, read them from dataset 'ff', which should have\n"
      "                2 float-valued sub-bricks.\n"
      "               * Note that the (a,b) values read from this file will\n"
      "                   be mapped to the nearest ones on the (a,b) grid\n"
      "                   before being used to solve the generalized least\n"
      "                   squares problem. For this reason, you may want\n"
      "                   to use '-Grid 5' to make the (a,b) grid finer, if\n"
      "                   you are not using (a,b) values from a -Rvar file.\n"
      "               * Using this option will skip the slowest part of\n"
      "                   the program, which is the scan for each voxel\n"
      "                   to find its optimal (a,b) parameters.\n"
      "               * One possible application of -ABfile:\n"
      "                  + save (a,b) using -Rvar in 3dREMLfit\n"
      "                  + process them in some way (spatial smoothing?)\n"
      "                  + use these modified values for fitting in 3dREMLfit\n"
      "                      [you should use '-Grid 5' for such a case]\n"
      "               * Another possible application of -ABfile:\n"
      "                  + use (a,b) from -Rvar to speed up a run with -Rglt\n"
      "                      when you want to run some more contrast tests.\n"
      "               * Special case:\n"
      "                     -ABfile =0.7,-0.3\n"
      "                   e.g., means to use a=0.7 and b=-0.3 for all voxels.\n"
      "                   The program detects this special case by looking for\n"
      "                   '=' as the first character of the string 'ff' and\n"
      "                   looking for a comma in the middle of the string.\n"
      "                   The values of a and b must be in the range -0.9..+0.9.\n"
      "                 * The purpose of this special case is to facilitate\n"
      "                     comparison with Software PrograMs that use the same\n"
      "                     temporal correlation structure for all voxels.\n"
      "\n"
      " -GOFORIT   = 3dREMLfit checks the regression matrix for tiny singular\n"
      "                values (as 3dDeconvolve does). If the matrix is too\n"
      "                close to being rank-deficient, then the program will\n"
      "                not proceed. You can use this option to force the\n"
      "                program to continue past such a failed collinearity\n"
      "                check, but you MUST check your results to see if they\n"
      "                make sense!\n"
      "              ** '-GOFORIT' is required if there are all zero columns\n"
      "                   in the regression matrix. However, at this time\n"
      "                   [15 Mar 2010], the all zero columns CANNOT come from\n"
      "                   the '-slibase' inputs.\n"
      "                 ** Nor from the '-dsort' inputs.\n"
      "              ** If there are all zero columns in the matrix, a number\n"
      "                   of WARNING messages will be generated as the program\n"
      "                   pushes forward in the solution of the linear systems.\n"
      "\n"
      "---------------------\n"
      "Miscellaneous Options  ~1~\n"
      "---------------------\n"
      " -quiet = turn off most progress messages :(\n"
      " -verb  = turn on more progress messages  :)\n"
#ifdef USING_MCW_MALLOC
      "            [including memory usage reports at various stages]\n"
#endif
      "\n"
      "==========================================================================\n"
      "===========  Various Notes (as if this help weren't long enough) =========\n"
      "==========================================================================\n"
      "\n"
      "------------------\n"
      "What is ARMA(1,1)?  ~1~\n"
      "------------------\n"
      "* The correlation coefficient r(k) of noise samples k units apart in time,\n"
      "    for k >= 1, is given by r(k) = lam * a^(k-1)\n"
      "    where                   lam  = (b+a)(1+a*b)/(1+2*a*b+b*b)\n"
      "    (N.B.: lam=a when b=0 -- AR(1) noise has r(k)=a^k for k >= 0)\n"
      "    (N.B.: lam=b when a=0 -- MA(1) noise has r(k)=b for k=1, r(k)=0 for k>1)\n"
      "\n"
      "* lam can be bigger or smaller than a, depending on the sign of b:\n"
      "    b > 0 means lam > a;  b < 0 means lam < a.\n"
      "\n"
      "* What I call (a,b) here is sometimes called (p,q) in the ARMA literature.\n"
      "\n"
      "* For a noise model which is the sum of AR(1) and white noise, 0 < lam < a\n"
      "    (i.e., a > 0  and  -a < b < 0 ). Thus, the model 'AR(1)+white noise'\n"
      "    is a proper subset of ARMA(1,1) -- and also a proper subset of the default\n"
      "    -POScor setting (which also allows 0 < a < lam via b > 0).\n"
      "  + This restricted model can be specified with the '-WNplus' option.\n"
      "    With '-WNplus', you should use '-Grid 5', since you are restricting\n"
      "    the number of available noise models fairly substantially.\n"
      "  + If the variance of the white noise is T and the variance of the AR(1) noise\n"
      "    is U, then lam = (a*U)/(U+T*(1-a^2)), and U/T = (lam*(1-a^2))/(a^2-lam).\n"
      "  + In principal, one could estimate the fraction of the noise that is\n"
      "    white vs. correlated using this U/T formula (e.g., via 3dcalc on the\n"
      "    '-Rvar' output).\n"
      "  + It is not clear that such an estimate is useful for any purpose,\n"
      "    or indeed that the '-Rvar' outputs of the ARMA(1,1) parameters\n"
      "    are useful for more than code testing reasons. YMMV :)\n"
      "\n"
      "* The natural range of a and b is -1..+1. However, unless -NEGcor is\n"
      "    given, only non-negative values of a will be used, and only values\n"
      "    of b that give lam > 0 will be allowed. Also, the program doesn't\n"
      "    allow values of a or b to be outside the range -0.9..+0.9.\n"
      "\n"
      "* The program sets up the correlation matrix using the censoring and run\n"
      "    start information saved in the header of the .xmat.1D matrix file, so\n"
      "    that the actual correlation matrix used will not always be Toeplitz.\n"
      "    For details of how time series with such gaps are analyzed, see the\n"
      "    math notes.\n"
      "\n"
      "* The 'Rvar' dataset has 5 sub-bricks with variance parameter estimates:\n"
      "    #0 = a = factor by which correlations decay from lag k to lag k+1\n"
      "    #1 = b parameter\n"
      "    #2 = lam (see the formula above) = correlation at lag 1\n"
      "    #3 = standard deviation of ARMA(1,1) noise in that voxel\n"
      "    #4 = -log(REML likelihood function) = optimized function at (a,b)\n"
      "         For details about this, see the math notes.\n"
      "\n"
      "* The 'Rbeta' dataset has the beta (model fit) parameters estimates\n"
      "    computed from the prewhitened time series data in each voxel,\n"
      "    as in 3dDeconvolve's '-cbucket' output, in the order in which\n"
      "    they occur in the matrix. -addbase and -slibase and -dsort beta\n"
      "    values come last in this file.\n"
       "   [The '-nobout' option will disable output of baseline parameters.]\n"
      "\n"
      "* The 'Rbuck' dataset has the beta parameters and their statistics\n"
      "    mixed together, as in 3dDeconvolve's '-bucket' output.\n"
      "\n"
      "-------------------------------------------------------------------\n"
      "What is REML = REsidual (or REstricted) Maximum Likelihood, anyway?  ~1~\n"
      "-------------------------------------------------------------------\n"
      "* Ordinary Least SQuares (which assumes the noise correlation matrix is\n"
      "    the identity) is consistent for estimating regression parameters,\n"
      "    but is NOT consistent for estimating the noise variance if the\n"
      "    noise is significantly correlated in time - 'serial correlation'\n"
      "    or 'temporal correlation'.\n"
      "\n"
      "* Maximum likelihood estimation (ML) of the regression parameters and\n"
      "    variance/correlation together is asymptotically consistent as the\n"
      "    number of samples goes to infinity, but the variance estimates\n"
      "    might still have significant bias at a 'reasonable' number of\n"
      "    data points.\n"
      "\n"
      "* REML estimates the variance/correlation parameters in a space\n"
      "    of residuals -- the part of the data left after the model fit\n"
      "    is subtracted. The amusing/cunning part is that the model fit\n"
      "    used to define the residuals is itself the generalized least\n"
      "    squares fit where the variance/correlation matrix is the one found\n"
      "    by the REML fit itself. This feature makes REML estimation nonlinear,\n"
      "    and the REML equations are usually solved iteratively, to maximize\n"
      "    the log-likelihood in the restricted space. In this program, the\n"
      "    REML function is instead simply optimized over a finite grid of\n"
      "    the correlation matrix parameters a and b. The matrices for each\n"
      "    (a,b) pair are pre-calculated in the setup phase, and then are\n"
      "    re-used in the voxel loop. The purpose of this grid-based method\n"
      "    is speed -- optimizing iteratively to a highly accurate (a,b)\n"
      "    estimation for each voxel would be very time consuming, and pretty\n"
      "    pointless. If you are concerned about the sensitivity of the\n"
      "    results to the resolution of the (a,b) grid, you can use the\n"
      "    '-Grid 5' option to increase this resolution and see if your\n"
      "    activation maps change significantly. In test cases, the resulting\n"
      "    betas and statistics have not changed appreciably between '-Grid 3'\n"
      "    and '-Grid 5'; however, you might want to test this on your own data\n"
      "    (just for fun, because who doesn't want more fun?).\n"
      "\n"
      "* REML estimates of the variance/correlation parameters are still\n"
      "    biased, but are generally significantly less biased than ML estimates.\n"
      "    Also, the regression parameters (betas) should be estimated somewhat\n"
      "    more accurately (i.e., with smaller variance than OLSQ). However,\n"
      "    this effect is generally small in FMRI data, and probably won't affect\n"
      "    your group results noticeably (if you don't carry parameter variance\n"
      "    estimates to the inter-subject analysis, as is done in 3dMEMA).\n"
      "\n"
      "* After the (a,b) parameters are estimated, then the solution to the\n"
      "    linear system is available via Generalized Least SQuares; that is,\n"
      "    via prewhitening using the Choleski factor of the estimated\n"
      "    variance/covariance matrix.\n"
      "\n"
      "* In the case with b=0 (that is, AR(1) correlations), and if there are\n"
      "    no time gaps (no censoring, no run breaks), then it is possible to\n"
      "    directly estimate the a parameter without using REML. This program\n"
      "    does not implement such a method (e.g., the Yule-Walker equation).\n"
      "    The reasons why should be obvious.\n"
      "\n"
      "* If you like linear algebra, see my scanned math notes about 3dREMLfit:\n"
      "    https://afni.nimh.nih.gov/pub/dist/doc/misc/3dREMLfit/3dREMLfit_mathnotes.pdf\n"
      "    https://afni.nimh.nih.gov/pub/dist/doc/htmldoc/statistics/remlfit.html\n"
      "\n"
      "* I have been asked if 3dREMLfit prewhitens the design matrix as well as\n"
      "    the data. The short answer to this somewhat uninformed question is YES.\n"
      "    The long answer follows (warning: math ahead!):\n"
      "\n"
      "* Mathematically, the GLSQ solution is expressed as\n"
      "    f = inv[ X' inv(R) X] X' inv(R) y\n"
      "    where X = model matrix, R = symmetric correlation matrix\n"
      "              of noise (R depends on the a,b parameters),\n"
      "          f = parameter estimates, and y = data vector.\n"
      "    Notation: ' = transpose, inv() = inverse matrix.\n"
      "    A symmetric matrix S such that SS = R is called a square root of R\n"
      "    (there are many such matrices). The matrix inv(S) is a prewhitening\n"
      "    matrix. That is, if the noise vector q is such that E(q q') = R\n"
      "    (here E = expected value), and vector t = inv(S) q, then\n"
      "    E(t t') = E[ inv(S)q q'inv(S) ] = inv(S) S S inv(S) = I.\n"
      "    Note that inv(R) = inv(S) inv(S), and we can rewrite the GLSQ solution as\n"
      "    f = inv[ X' inv(S) inv(S) X ] X' inv(S) inv(S) y\n"
      "      = inv[ (inv(S)X)' (inv(S)X) ] (inv(S)X)' (inv(S)y)\n"
      "    so the GLSQ solution is equivalent to the OLSQ solution, with the model\n"
      "    matrix X replaced by inv(S)X and the data vector y replaced by inv(S)y;\n"
      "    that is, we prewhiten both of them. In 3dREMLfit, this is done implicitly\n"
      "    in the solution method outlined in the 7-step procedure on the fourth page\n"
      "    of my math notes -- a procedure designed for efficient implementation\n"
      "    with banded R. The prewhitened X matrix is never explicitly computed:\n"
      "    it is not needed, since the goal is to compute vector f, not inv(S)X.\n"
      "\n"
      "* The idea of pre-whitening the data but NOT the matrix is a very bad plan.\n"
      "    (This also was a suggestion by a not-well-informed user.)\n"
      "    If you work through the linear algebra, you'll see that the resulting\n"
      "    estimate for f is not statistically consistent with the underlying model!\n"
      "    In other words, prewhitening only the data but not the matrix is WRONG.\n"
      "\n"
      "* Someone asking the question above might actually be asking if the residuals\n"
      "    are whitened. The answer is YES and NO. The output of -Rerrts is not\n"
      "    whitened; in the above notation, -Rerrts gives y-Xf = data - model fit.\n"
      "    The output of -Rwherr is whitened; -Rwherr gives S[y-Xf], which is the\n"
      "    residual (eps) vector for the pre-whitened linear system Sf = SXf + eps.\n"
      "\n"
      "* The estimation method for (a,b) is nonlinear; that is, these parameters\n"
      "    are NOT estimated by doing an initial OLSQ (or any other one-shot initial\n"
      "    calculation), then fitting (a,b) to the resulting residuals. Rather,\n"
      "    a number of different (a,b) values are tried out to find the parameter pair\n"
      "    where the log-likelihood of the Gaussian model is optimized. To be precise,\n"
      "    the function that is minimized in each voxel (over the discrete a,b grid) is\n"
      "      L(a,b) =  log(det(R(a,b))) + log(det(X' inv(R(a,b)) X))\n"
      "              + (n-m)log(y'P(a,b)y)   - log(det(X'X'))\n"
      "    where R(a,b) = ARMA(1,1) correlation matrix (symmetric n X n)\n"
      "          n      = dimension of data vector = number of rows in X\n"
      "          m      = number of columns in X = number of regressors\n"
      "          y      = data vector for a given voxel\n"
      "          P(a,b) = prewhitening projection matrix (symmetric n X n)\n"
      "                 = inv(R) - inv(R)X inv(X' inv(R) X) X' inv(R)\n"
      "    The first 2 terms in L only depend on the (a,b) parameters, and can be\n"
      "      thought of as a penalty that favors some (a,b) values over others,\n"
      "      independent of the data -- for ARMA(1,1), the a=b=0 white noise\n"
      "      model is penalized somewhat relative to the non-white noise cases.\n"
      "    The 3rd term uses the 2-norm of the prewhitened residuals.\n"
      "    The 4th term depends only on X, and is not actually used herein, since\n"
      "    we don't include a model for varying X as well as R.\n"
      "\n"
      "* The method for estimating (a,b) does not require the time series data to be\n"
      "    perfectly uniform in time. Gaps due to censoring and run break are allowed\n"
      "    for properly.\n"
      "\n"
      "* Again, see the math notes for more fun fun algorithmic details:\n"
      "    https://afni.nimh.nih.gov/pub/dist/doc/misc/3dREMLfit/3dREMLfit_mathnotes.pdf\n"
      "    https://afni.nimh.nih.gov/pub/dist/doc/htmldoc/statistics/remlfit.html\n"
      "\n"
      "----------------\n"
      "Other Commentary ~1~\n"
      "----------------\n"
      "* Again: the ARMA(1,1) parameters 'a' (AR) and 'b' (MA) are estimated\n"
      "    only on a discrete grid, for the sake of CPU time.\n"
      "\n"
      "* Each voxel gets a separate pair of 'a' and 'b' parameters.\n"
      "    There is no option to estimate global values for 'a' and 'b'\n"
      "    and use those for all voxels. Such an approach might be called\n"
      "    'kindergarten statistics' by the promulgators of Some People's Methods.\n"
      "\n"
      "* OLSQ = Ordinary Least SQuares; these outputs can be used to compare\n"
      "         the REML/GLSQ estimations with the simpler OLSQ results\n"
      "         (and to test this program vs. 3dDeconvolve).\n"
      "\n"
      "* GLSQ = Generalized Least SQuares = estimated linear system solution\n"
      "         taking into account the variance/covariance matrix of the noise.\n"
      "\n"
      "* The '-matrix' file must be from 3dDeconvolve; besides the regression\n"
      "    matrix itself, the header contains the stimulus labels, the GLTs,\n"
      "    the censoring information, etc.\n"
      "  + Although you can put in a 'raw' matrix using the '-matim' option,\n"
      "     described earlier.\n"
      "\n"
      "* If you don't actually want the OLSQ results from 3dDeconvolve, you can\n"
      "    make that program stop after the X matrix file is written out by using\n"
      "    the '-x1D_stop' option, and then running 3dREMLfit; something like this:\n"
      "      3dDeconvolve -bucket Fred -nodata 800 2.5 -x1D_stop ...\n"
      "      3dREMLfit -matrix Fred.xmat.1D -input ...\n"
      "    In the above example, no 3D dataset is input to 3dDeconvolve, so as to\n"
      "    avoid the overhead of having to read it in for no reason. Instead,\n"
      "    the '-nodata 800 2.5' option is used to setup the time series of the\n"
      "    desired length (corresponding to the real data's length, here 800 points),\n"
      "    and the appropriate TR (here, 2.5 seconds). This will properly establish\n"
      "    the size and timing of the matrix file.\n"
      "\n"
      "* The bucket output datasets are structured to mirror the output\n"
      "    from 3dDeconvolve with the default options below:\n"
      "      -nobout -full_first\n"
      "    Note that you CANNOT use options like '-bout', '-nocout', and\n"
      "    '-nofull_first' with 3dREMLfit -- the bucket datasets are ordered\n"
      "    the way they are and you'll just have to live with it.\n"
      "\n"
      "* If the 3dDeconvolve matrix generation step did NOT have any non-base\n"
      "    stimuli (i.e., everything was '-stim_base'), then there are no 'stimuli'\n"
      "    in the matrix file. In that case, since by default 3dREMLfit doesn't\n"
      "    compute statistics of baseline parameters, to get statistics you will\n"
      "    have to use the '-gltsym' option here, specifying the desired column\n"
      "    indexes with the 'Col[]' notation, and then use '-Rglt' to get these\n"
      "    values saved somewhere (since '-Rbuck' won't work if there are no\n"
      "    'Stim attributes').\n"
      "\n"
      "* All output datasets are in float format [i.e., no '-short' option].\n"
      "    Internal calculations are done in double precision.\n"
      "\n"
      "* If the regression matrix (including any added columns from '-addbase'\n"
      "    or '-slibase') is rank-deficient (e.g., has collinear columns),\n"
      "    then the program will print a message something like\n"
      "      ** ERROR: X matrix has 1 tiny singular value -- collinearity\n"
      "    The program will NOT continue past this type of error, unless\n"
      "    the '-GOFORIT' option is used. You should examine your results\n"
      "    carefully to make sure they are reasonable (e.g., look at\n"
      "    the fitted model overlay on the input time series).\n"
      "\n"
      "* The Ljung-Box (LB) statistic computed via the '-Rvar' option is a\n"
      "    measure of how correlated the ARMA(1,1) pre-whitened residuals are\n"
      "    in time. A 'small' value indicates that the pre-whitening was\n"
      "    reasonably successful (e.g., small LB = 'good').\n"
      "  + The LB volume will be marked as a chi-squared statistic with h-2 degrees\n"
      "     of freedom, where 'h' is the semi-arbitrarily chosen maximum lag used.\n"
      "     A large LB value indicates noticeable temporal correlation in the\n"
      "     pre-whitened residuals (e.g., that the ARMA(1,1) model wasn't adequate).\n"
      "  + If a voxel has LB statistic = 0, this means that the LB value could not\n"
      "     be computed for some reason (e.g., residuals are all zero).\n"
      "  + For yet more information, see this article:\n"
      "      On a measure of lack of fit in time series models.\n"
      "      GM Ljung, GEP Box. Biometrika, 1978.\n"
      "      https://www.jstor.org/stable/2335207\n"
      "      https://academic.oup.com/biomet/article/65/2/297/236869\n"
      "  + The calculation of the LB statistic is adjusted to allow for gaps in\n"
      "     the time series (e.g., censoring, run gaps).\n"
      "  + Note that the LB statistic is computed if and only if you give the\n"
      "     '-Rvar' option. You don't have to give the '-Rwherr' option, which is\n"
      "     used to save the pre-whitened residuals to a dataset.\n"
      "  + If you want to test the LB statistic calculation under the null\n"
      "     hypothesis (i.e., that the ARMA(1,1) model is correct), then\n"
      "     you can use program 3dSimARMA11 to create a time series dataset,\n"
      "     then run that through 3dREMLfit, then peruse the histogram\n"
      "     of the resulting LB statistic. Have fun!\n"
      "\n"
      "* Depending on the matrix and the options, you might expect CPU time\n"
      "    to be about 2..4 times that of the corresponding 3dDeconvolve run.\n"
      "  + A careful choice of algorithms for solving the multiple linear\n"
      "     systems required (e.g., QR method, sparse matrix operations,\n"
      "     bordering, etc.) and some other code optimizations make\n"
      "     running 3dREMLfit tolerable.\n"
      "  + Especially on modern fast CPUs. Kids these days have NO idea\n"
      "     about how we used to suffer waiting for computer runs, and\n"
      "     how we passed the time by walking uphill through the snow.\n"
      "\n"
      "---------------------------------------------------------------\n"
      "How 3dREMLfit handles all zero columns in the regression matrix\n"
      "---------------------------------------------------------------\n"
#if 0
      "* 3dDeconvolve uses the singular value decomposition to compute\n"
      "    the pseudo-inverse of the regression matrix X; normally, the\n"
      "    pseudo-inverse is given by inv[X'X] X'. However, if X has\n"
      "    any zero singular values (i.e., there is a linear combination\n"
      "    of the columns of X that is zero), then matrix X'X is not\n"
      "    invertible. The SVD-based pseudo-inverse of X proceeds past\n"
      "    this point by modifying X'X to make it non-singular, by\n"
      "    boosting up the close-to-zero singular values to make them\n"
      "    bounded away from zero.\n"
      "* 3dREMLfit doesn't use the SVD and doesn't form the pseudo-inverse\n"
      "    for the calculations for several reasons -- to save on CPU time\n"
      "    and to save on memory, since many more matrices must be processed\n"
      "    than in 3dDeconvolve. Instead, 3dREMLfit uses the QR factorization\n"
      "    [of R^(-1/2)X]. However, if X has any zero singular values, then\n"
      "    the some diagonal elements of the QR factor will be zero, and in\n"
      "    in the equation solution process, we have to divide by these diagonal\n"
      "    elements ==> problem!\n"
      "* The solution used here is to 'de-singularize' the matrix before QR\n"
      "    factorization (if '-GOFORIT' is on). This is done simply by\n"
      "    doing the SVD of the matrix to be QR factored, boosting up the\n"
      "    close-to-zero singular values, and then re-constructing the matrix\n"
      "    from the SVD factors.\n"
#endif
      "* One salient (to the user) difference from 3dDeconvolve is how\n"
      "    3dREMLfit deals with the beta weight from an all zero column when\n"
      "    computing a statistic (e.g., a GLT). The beta weight will simply\n"
      "    be ignored, and its entry in the GLT matrix will be set to zero.\n"
      "    Any all zero rows in the GLT matrix are then removed. For example,\n"
      "    the 'Full_Fstat' for a model with 3 beta weights is computed from\n"
      "    the GLT matrix [ 1 0 0 ]\n"
      "                   [ 0 1 0 ]\n"
      "                   [ 0 0 1 ].  If the last beta weight corresponds to\n"
      "    an all zero column, then the matrix becomes [ 1 0 0 ]\n"
      "                                                [ 0 1 0 ]\n"
      "                                                [ 0 0 0 ], and then\n"
      "    then last row is omitted. This excision reduces the number of\n"
      "    numerator degrees of freedom in this test from 3 to 2. The net\n"
      "    effect is that the F-statistic will be larger than in 3dDeconvolve,\n"
      "    which does not modify the GLT matrix (or its equivalent).\n"
      "\n"
      " * A similar adjustment is made to denominator degrees of freedom, which\n"
      "    is usually n-m, where n=# of data points and m=# of regressors.\n"
      "    3dDeconvolve counts all zero regressors in with m, but 3dREMLfit\n"
      "    does not. The net effect is again to (slightly) increase F-statistic\n"
      "    values over the equivalent 3dDeconvolve computation.\n"
      "\n"
      "-----------------------------------------------------------\n"
      "To Dream the Impossible Dream, to Write the Uncodeable Code\n"
      "-----------------------------------------------------------\n"
      "* Add options for -iresp/-sresp for -stim_times.\n"
      "* Prevent Daniel Glen from referring to this program as 3dARMAgeddon.\n"
      "* Establish incontrovertibly the nature of quantum mechanical observation.\n"
      "* Create an iPad version of the AFNI software suite.\n"
      "* Get people to stop asking me 'quick questions'!\n"
      "\n"
      "----------------------------------------------------------\n"
      "* For more information, please see the contents of\n"
      "    https://afni.nimh.nih.gov/pub/dist/doc/misc/3dREMLfit/3dREMLfit_mathnotes.pdf\n"
      "  which includes comparisons of 3dDeconvolve and 3dREMLfit\n"
      "  activations (individual subject and group maps), and an\n"
      "  outline of the mathematics implemented in this program.\n"
      "----------------------------------------------------------\n"
      "\n"
      "============================\n"
      "== RWCox - July-Sept 2008 ==\n"
      "============================\n"

      , corcut  /* correlation cutoff, print outed far above */
     ) ;

     PRINT_AFNI_OMP_USAGE("3dREMLfit",
       "* The REML matrix setup and REML voxel ARMA(1,1) estimation loops are\n"
       "   parallelized, across (a,b) parameter sets and across voxels, respectively.\n"
       "* The GLSQ and OLSQ loops are not parallelized. They are usually much\n"
       "   faster than the REML voxel loop, and so I made no effort to speed\n"
       "   these up (now and forever, two and inseparable).\n"
       "* '-usetemp' disables OpenMP multi-CPU usage, since the file I/O for\n"
       "   saving and restoring various matrices and results is not easily\n"
       "   parallelized. To get OpenMP speedup for large problems (just where\n"
       "   you want it), you'll need a lot of RAM.\n"
     ) ;

     PRINT_COMPILE_DATE ; exit(0) ;
   }

   /*--------------------------- official startup --------------------------*/

#if defined(USING_MCW_MALLOC) && !defined(USE_OMP)
   enable_mcw_malloc() ;
#endif

   PRINT_VERSION("3dREMLfit"); mainENTRY("3dREMLfit main"); machdep();
   AFNI_logger("3dREMLfit",argc,argv); AUTHOR("RWCox");
   SET_message_file("3dREMLfit.err") ;
   (void)COX_clock_time() ;
   THD_check_AFNI_version("3dREMLfit");

   /**------- scan command line --------**/

   if( AFNI_yesenv("AFNI_3dDeconvolve_GOFORIT") ) goforit++ ;

   if( AFNI_yesenv("AFNI_3dDeconvolve_VERB") ) verb++ ;  /* 19 May 2020 */

   iarg = 1 ;
   while( iarg < argc ){

     if( strcasecmp(argv[iarg],"-qsumq") == 0 ){     /* HIDDEN [28 Apr 2020] */
       do_logqsumq = 0 ; iarg++ ; continue ;         /* should NOT BE USED */
     }

     /**==========   -virtvec  ==========**/

     if( strcasecmp(argv[iarg],"-virtvec") == 0 ){   /** for Javier -- 13 Dec 2012 **/
       virtu_mrv = 1 ; iarg++ ; continue ;
     }

     /**==========   -gltsym  ==========**/

     if( strcasecmp(argv[iarg],"-gltsym") == 0 ){
       if( ++iarg >= argc-1 ) ERROR_exit("Need 2 arguments after '%s'",argv[iarg-1]) ;
       eglt_sym = (char **)realloc( eglt_sym , sizeof(char *)*(eglt_num+1) ) ;
       eglt_lab = (char **)realloc( eglt_lab , sizeof(char *)*(eglt_num+1) ) ;
       eglt_sym[eglt_num] = argv[iarg++] ;
       if( argv[iarg][0] != '-' ){
         eglt_lab[eglt_num] = argv[iarg++] ;
       } else {
         eglt_lab[eglt_num] = (char *)malloc(sizeof(char)*16) ;
         sprintf( eglt_lab[eglt_num] , "GLTsym%02d" , eglt_num+1 ) ;
       }
       eglt_num++ ; continue ;
     }

     /**==========   verbosity  ==========**/

     if( strcasecmp(argv[iarg],"-verb") == 0 ){
       verb++ ; iarg++ ; continue ;
     }
     if( strcasecmp(argv[iarg],"-quiet") == 0 ){
       verb = 0 ; iarg++ ; continue ;
     }

     /**==========  -GOFORIT babee!  =========**/

     if( strcasecmp(argv[iarg],"-GOFORIT") == 0 ){  /* 19 Dec 2008 */
       iarg++ ;
       if( iarg < argc && isdigit(argv[iarg][0]) )
         goforit += (int)strtod(argv[iarg++],NULL) ;
       else
         goforit++ ;
       continue ;
     }

     /**==========   -nodmbase and -usetemp  ==========**/

     if( strcasecmp(argv[iarg],"-nodmbase") == 0 ){
       dmbase = 0 ; iarg++ ; continue ;
     }
     if( strcasecmp(argv[iarg],"-usetemp") == 0 ){
       usetemp = 1 ; iarg++ ; continue ;
     }

     /**==========   -dsort_nods    ==========**/

     if( strcasecmp(argv[iarg],"-dsort_nods") == 0 ){
       dsort_nods = 1 ; iarg++ ; continue ;
     }

     /**==========   -dsort    ==========**/

     if( strcasecmp(argv[iarg],"-dsort") == 0 ){
       THD_3dim_dataset *dsim ;
       if( ++iarg >= argc ) ERROR_exit("Need argument after option '%s'",argv[iarg-1]) ;
       if( dsortar == NULL ) INIT_3DARR(dsortar) ;
       dsim = THD_open_dataset(argv[iarg]) ; CHECK_OPEN_ERROR(dsim,argv[iarg]) ;
       ADDTO_3DARR(dsortar,dsim) ;
       iarg++ ; continue ;
     }

     /**==========   -addbase  ==========**/

     if( strcasecmp(argv[iarg],"-addbase") == 0 ){
       MRI_IMAGE *im ;
       if( ++iarg >= argc ) ERROR_exit("Need argument after '%s'",argv[iarg-1]) ;
       do{
         im = mri_read_1D( argv[iarg] ) ;
         if( im == NULL ) ERROR_exit("Cannot read -addbase file '%s'",argv[iarg]) ;
         if( imar_addbase == NULL ) INIT_IMARR(imar_addbase) ;
         mri_add_name( THD_trailname(argv[iarg],0) , im ) ;
         ADDTO_IMARR( imar_addbase , im ) ;
         ncol_addbase += im->ny ;  /* number of columns to add to the matrix */
         iarg++ ;
       } while( iarg < argc && argv[iarg][0] != '-' ) ;
       continue ;
     }

     /**==========   -slibase  ==========**/

     if( strcasecmp(argv[iarg],"-slibase") == 0 ){
       MRI_IMAGE *im ;
       int label_order ;
       if( ++iarg >= argc ) ERROR_exit("Need argument after '%s'",argv[iarg-1]) ;
       do{
         im = mri_read_1D( argv[iarg] ) ;
         if( im == NULL ) ERROR_exit("Cannot read -slibase file '%s'",argv[iarg]) ;
         /* if known, require slice-minor order of regressors 28 Jul 2009 [r] */
         label_order = niml_get_major_label_order(argv[iarg]);
         if( label_order != 2 ) {  /* not slice-minor order */
           if( label_order == 1 ) {
             ERROR_exit("Label ordering of -slibase dataset is slice-major,\n"
                 "   for which -slibase_sm is more appropriate. If this is\n"
                 "   not clear, search for it on the AFNI Message Board:\n"
                 "        https://discuss.afni.nimh.nih.gov");
           } else { /* order is unknown */
             WARNING_message("Unknown regressor ordering. If the regressors\n"
                 "   were made via 'RetroTS', perhaps -slibase_sm is more\n"
                 "   appropriate than -slibase.");
           }
         } /* end label_order check */
         if( imar_slibase == NULL ) INIT_IMARR(imar_slibase) ;
         mri_add_name( THD_trailname(argv[iarg],0) , im ) ;
         ADDTO_IMARR( imar_slibase , im ) ;
         nfile_slibase++ ;
         iarg++ ;
       } while( iarg < argc && argv[iarg][0] != '-' ) ;
       continue ;
     }

     /**==========   -slibase_sm (slice-major order) ==========**/
     /**                                   27 Jul 2009 [rickr] **/

     if( strcasecmp(argv[iarg],"-slibase_sm") == 0 ){
       MRI_IMAGE *im, *newim;
       int       nz, label_order ;
       if( ++iarg >= argc ) ERROR_exit("Need argument after '%s'",argv[iarg-1]);
       if( !inset ) ERROR_exit("-slibase_sm must follow -input");
       nz = DSET_NZ(inset);
       do{
         im = mri_read_1D( argv[iarg] ) ;
         if( im == NULL )
            ERROR_exit("Cannot read -slibase_sm file '%s'", argv[iarg]) ;
         /* if known, require slice-major order of regressors 28 Jul 2009 [r] */
         label_order = niml_get_major_label_order(argv[iarg]);
         if( label_order != 1 ) {  /* not slice-major order */
           if( label_order == 2 ) {
             ERROR_exit("Label order of -slibase_sm dataset is slice-minor,\n"
                 "   for which -slibase is more appropriate. If this is\n"
                 "   not clear, search for it on the AFNI Message Board:\n"
                 "        https://discuss.afni.nimh.nih.gov");
           } else { /* order is unknown */
             WARNING_message("Unknown regressor ordering. If the regressors\n"
                 "   were made via 'RetroTS' you are probably okay.");
           }
         } /* end label_order check */
         if( imar_slibase == NULL ) INIT_IMARR(imar_slibase) ;
         /* convert the slice-major column order to slice-minor */
         if( (newim = mri_interleave_columns(im, nz) ) == NULL ) exit(1) ;
         mri_free(im) ; im = newim ;
         mri_add_name( THD_trailname(argv[iarg],0) , im ) ;
         ADDTO_IMARR( imar_slibase , im ) ;
         nfile_slibase++ ;
         iarg++ ;
       } while( iarg < argc && argv[iarg][0] != '-' ) ;
       continue ;
     }

     /**==========   -noFDR and -FDR  ==========**/

     if( strcasecmp(argv[iarg],"-noFDR") == 0 ){
       do_FDR = 0 ; iarg++ ; continue ;
     }
     if( strcasecmp(argv[iarg],"-FDR") == 0 ){
       do_FDR = 1 ; iarg++ ; continue ;
     }

     /**==========   -ABfile  ==========**/

     if( strcasecmp(argv[iarg],"-ABfile") == 0 ){
       if( abset != NULL || abfixed )
         ERROR_exit("Cannot have 2 '%s' options",argv[iarg]) ;
       if( ++iarg >= argc ) ERROR_exit("Need argument after '%s'",argv[iarg-1]) ;
       if( argv[iarg][0] == '=' && strchr(argv[iarg],',') != NULL ){
         afix = bfix = -66.0f ;
         sscanf( argv[iarg] , "=%f,%f" , &afix , &bfix ) ;
         if( afix < -0.9f || afix > 0.9f ||
             bfix < -0.9f || bfix > 0.9f   )
           ERROR_exit("%s %s has illegal fixed values of a and/or b (outside range -0.9..0.9)!",
                      argv[iarg-1] , argv[iarg] ) ;
         abfixed = 1 ;
       } else {
         abset = THD_open_dataset( argv[iarg] ) ;
         CHECK_OPEN_ERROR(abset,argv[iarg]) ;
       }
       iarg++ ; continue ;
     }

      /**==========   ARMA params  ==========**/

     if( strcasecmp(argv[iarg],"-MAXrho") == 0 || strcasecmp(argv[iarg],"-MAXa") == 0 ){
       if( ++iarg >= argc ) ERROR_exit("Need argument after '%s'",argv[iarg-1]) ;
       rhomax = strtod(argv[iarg],NULL) ;
            if( rhomax < 0.1 ){ rhomax = 0.1; WARNING_message("-MAXa re-set to 0.1 from %s",argv[iarg]); }
       else if( rhomax > 0.9 ){ rhomax = 0.9; WARNING_message("-MAXa re-set to 0.9 from %s",argv[iarg]); }
       rm_set = rhomax ; iarg++ ; continue ;
     }

     if( strcasecmp(argv[iarg],"-Grid") == 0 ){
       if( ++iarg >= argc ) ERROR_exit("Need argument after '%s'",argv[iarg-1]) ;
       nlevab = (int)strtod(argv[iarg],NULL) ;
            if( nlevab < 3 ){ nlevab = 3; WARNING_message("-Grid re-set to 3 from %s",argv[iarg]); }
       else if( nlevab > 7 ){ nlevab = 7; WARNING_message("-Grid re-set to 7 from %s",argv[iarg]); }
       iarg++ ; continue ;
     }

     if( strcasecmp(argv[iarg],"-MAXb") == 0 ){
       if( ++iarg >= argc ) ERROR_exit("Need argument after '%s'",argv[iarg-1]) ;
       bmax = strtod(argv[iarg],NULL) ;
            if( bmax < 0.1 ){ bmax = 0.1; WARNING_message("-MAXb re-set to 0.1 from %s",argv[iarg]); }
       else if( bmax > 0.9 ){ bmax = 0.9; WARNING_message("-MAXb re-set to 0.9 from %s",argv[iarg]); }
       bm_set = bmax ; iarg++ ; continue ;
     }

     /*-- the next 3 options are actually implemented inside remla.c --*/

     if( strcasecmp(argv[iarg],"-NEGcor") == 0 ){
       REML_allow_negative_correlations(1) ;
       ININFO_message("negative correlations enabled") ;
       iarg++ ; continue ;
     }

     if( strcasecmp(argv[iarg],"-POScor") == 0 ){
       REML_allow_negative_correlations(0) ;
       ININFO_message("negative correlations disabled") ;
       iarg++ ; continue ;
     }

     if( strcasecmp(argv[iarg],"-WNplus") == 0 ){   /* 02 Jan 2020 */
       REML_allow_only_pos_white_noise(1) ;
       ININFO_message("AR(1)+white noise model enabled") ;
       iarg++ ; continue ;
     }

     /*---*/

     if( strcasecmp(argv[iarg],"-Mfilt") == 0 ){
       if( ++iarg >= argc ) ERROR_exit("Need argument after '%s'",argv[iarg-1]) ;
       mfilt_radius = (float)strtod(argv[iarg],NULL) ;
       do_mfilt = (mfilt_radius != 0.0f) ;
       do_dxyz  = (mfilt_radius > 0.0f) ;
       mri_medianfilter_usedxyz( do_dxyz ) ;
       if( mfilt_radius < 0.0f ) mfilt_radius = -mfilt_radius ;
       iarg++ ; continue ;
     }

     if( strcasecmp(argv[iarg],"-CORcut") == 0 ){
       if( ++iarg >= argc ) ERROR_exit("Need argument after '%s'",argv[iarg-1]) ;
       dx = (float)strtod(argv[iarg],NULL) ;
       if( dx > 0.0f && dx <= 0.01f ) corcut = (double)dx ;
       else WARNING_message("Illegal value after -CORcut -- ignoring it (should be between 0 and 0.01)") ;
       iarg++ ; continue ;
     }

     /**==========   -matrix  ==========**/

     if( strcasecmp(argv[iarg],"-matrix") == 0 ){
       if( ++iarg >= argc ) ERROR_exit("Need argument after '%s'",argv[iarg-1]) ;
       if( nelmat != NULL ) ERROR_exit("More than 1 -matrix option!?");
       matname = argv[iarg];
#if 1
       nelmat = NI_read_element_fromfile( matname ) ; /* read NIML file */
#else
       { char *buf = AFNI_suck_file( matname ) ;
         if( buf == NULL )
           ERROR_exit("Cannot read any data from -matrix file '%s' :(",matname) ;
         nelmat = NI_read_element_fromstring( buf ) ;
         free(buf) ;
       }
#endif
       if( nelmat == NULL || nelmat->type != NI_ELEMENT_TYPE )
         ERROR_exit("Cannot parse/understand contents of -matrix file '%s' :(",matname) ;
       iarg++ ; continue ;
     }

      /**==========   -polort P [undocumented] ===========**/

     if( strcasecmp(argv[iarg],"-polort") == 0 ){
       char *qpt ;
       if( ++iarg >= argc ) ERROR_exit("Need argument after '%s'",argv[iarg-1]) ;
       polort = (int)strtod(argv[iarg],&qpt) ;
       if( *qpt != '\0' ) WARNING_message("Illegal non-numeric value after -polort") ;
       iarg++ ; continue ;
     }

      /**==========   -matim M [undocumented] ===========**/

     if( strcasecmp(argv[iarg],"-matim") == 0 ){
       if( ++iarg >= argc ) ERROR_exit("Need argument after '%s'",argv[iarg-1]) ;
       matim = mri_read_1D(argv[iarg]) ;
       if( matim == NULL ) ERROR_exit("-matim fails to read file '%s'",argv[iarg]) ;
       iarg++ ; continue ;
     }

      /**==========   -input  ==========**/

     if( strcasecmp(argv[iarg],"-input") == 0 ){
       if( inset != NULL  ) ERROR_exit("Cannot have two -input options!?") ;
       if( ++iarg >= argc ) ERROR_exit("Need argument after '%s'",argv[iarg-1]) ;
       inset = THD_open_dataset( argv[iarg] ) ;
       CHECK_OPEN_ERROR(inset,argv[iarg]) ;
       iarg++ ; continue ;
     }

     /**==========  -STATmask [15 Jul 2010]  ==========*/

     if( strcasecmp(argv[iarg],"-STATmask") == 0 ||
         strcasecmp(argv[iarg],"-FDRmask")  == 0   ){
       if( statmask != NULL ) ERROR_exit("You cannot use -STATmask twice") ;
       if( ++iarg >= argc ) ERROR_exit("Need argument after '%s'",argv[iarg-1]) ;
       statmask_name = strdup(argv[iarg]) ;
       statmask = THD_create_mask_from_string(statmask_name) ;
       if( statmask == NULL ){
         WARNING_message("-STATmask being ignored: cannot use it") ;
         free(statmask_name) ; statmask_name = NULL ;
       }
       iarg++ ; continue ;
     }

     /**==========   -mask  ==========**/

     if( strcasecmp(argv[iarg],"-mask") == 0 ){
       THD_3dim_dataset *mset ;
       if( ++iarg >= argc ) ERROR_exit("Need argument after '-mask'") ;
       if( mask != NULL || automask ) ERROR_exit("Cannot have two mask inputs") ;
       mset = THD_open_dataset( argv[iarg] ) ;
       CHECK_OPEN_ERROR(mset,argv[iarg]) ;
       DSET_load(mset) ; CHECK_LOAD_ERROR(mset) ;
       mask_nx = DSET_NX(mset); mask_ny = DSET_NY(mset); mask_nz = DSET_NZ(mset);
       mask = THD_makemask( mset , 0 , 0.5f, 0.0f ) ; DSET_delete(mset) ;
       if( mask == NULL ) ERROR_exit("Cannot make mask from dataset '%s'",argv[iarg]) ;
       nmask = THD_countmask( mask_nx*mask_ny*mask_nz , mask ) ;
       if( verb || nmask < 1 ) INFO_message("Number of voxels in mask = %d",nmask) ;
       if( nmask < 1 ) ERROR_exit("Mask is too small to process") ;
       iarg++ ; continue ;
     }

     if( strcasecmp(argv[iarg],"-automask") == 0 ){
       if( mask != NULL ) ERROR_exit("Cannot have -automask and -mask") ;
       automask = 1 ; iarg++ ; continue ;
     }

     /**==========   statistics options  ==========**/

     if( strcasecmp(argv[iarg],"-fout") == 0 ){
       do_fstat = 1 ; iarg++ ; continue ;
     }
     if( strcasecmp(argv[iarg],"-tout") == 0 ){
       do_tstat = 1 ; iarg++ ; continue ;
     }
     if( strcasecmp(argv[iarg],"-rout") == 0 ){  /* 23 Oct 2008 */
       do_rstat = 1 ; iarg++ ; continue ;
     }
     if( strcasecmp(argv[iarg],"-nobout") == 0 ){ /* 25 Mar 2009 */
       nobout = 1 ; iarg++ ; continue ;
     }

     /**==========   prefix options  ==========**/

#undef  PREFIX_OPTION
#define PREFIX_OPTION(vnam)                                      \
     if( strncasecmp(argv[iarg], "-" #vnam ,5) == 0 ){           \
       if( ++iarg >= argc )                                      \
         ERROR_exit("Need argument after '%s'",argv[iarg-1]) ;   \
       vnam = strdup(argv[iarg]) ;                               \
       if( (#vnam)[0] == 'R' ) nprefixR++ ; else nprefixO++ ;    \
       if( !THD_filename_ok(vnam) )                              \
         ERROR_exit("Illegal string after %s",argv[iarg-1]) ;    \
       iarg++ ; continue ;                                       \
     }

     PREFIX_OPTION(Rbeta_prefix)  ;
     PREFIX_OPTION(Rvar_prefix)   ;
     PREFIX_OPTION(Rbuckt_prefix) ;

     PREFIX_OPTION(Obeta_prefix)  ;
     PREFIX_OPTION(Ovar_prefix)   ;
     PREFIX_OPTION(Obuckt_prefix) ;
     PREFIX_OPTION(Rfitts_prefix) ;
     PREFIX_OPTION(Ofitts_prefix) ;

     PREFIX_OPTION(Rerrts_prefix) ;
     PREFIX_OPTION(Oerrts_prefix) ;
     PREFIX_OPTION(Rwherr_prefix) ;

     PREFIX_OPTION(Rglt_prefix) ;
     PREFIX_OPTION(Oglt_prefix) ;

     /**==========   bad users must be punished  ==========**/

     ERROR_exit("Unknown option '%s'",argv[iarg]) ;
   }

   /*------ The Ides of March 2010 ------*/

   if( goforit ){
     matrix_allow_desing(1) ;
     if( verb > 1 )
       INFO_message("GOFORIT ==> Matrix de-singularization is engaged!") ;
   }

STATUS("options done") ;

   /**--------------- sanity checks, dataset input, maskifying --------------**/

   if( inset             == NULL ) ERROR_exit("No -input dataset?!") ;
   if( nprefixR+nprefixO == 0    ) ERROR_exit("No output datasets at all?!") ;
   if( nprefixR          == 0    ){
     WARNING_message("No -R* output datasets? Skipping REML(a,b) estimation!") ;
     afix = bfix = 0.0f ; abfixed = 1 ;
     if( abset != NULL ){ DSET_delete(abset) ; abset = NULL ; }
   }

   if( dsort_nods && dsortar == NULL ){  /* 27 Jul 2015 */
     WARNING_message("-dsort_nods given without -dsort ==> ignoring it") ;
     dsort_nods = 0 ;
   }

   nvals = DSET_NVALS(inset) ; nvox = DSET_NVOX(inset) ;
   dx = fabsf(DSET_DX(inset)) ; nx = DSET_NX(inset) ;
   dy = fabsf(DSET_DY(inset)) ; ny = DSET_NY(inset) ; nxy = nx*ny ;
   dz = fabsf(DSET_DZ(inset)) ; nz = DSET_NZ(inset) ;

   /* check -dsort datasets, if any [22 Jul 2015] */

   if( dsortar != NULL ){
     int dd ;
     for( nbad=dd=0 ; dd < dsortar->num ; dd++ ){
       if( DSET_NVALS(dsortar->ar[dd]) != nvals ||
           DSET_NVOX (dsortar->ar[dd]) != nvox    ){
         ERROR_message("-dsort dataset '%s' doesn't match input dataset '%s'",
                       DSET_HEADNAME(dsortar->ar[dd]) , DSET_HEADNAME(inset) ) ;
         ININFO_message("      nt[dsort]=%d  nt[input] = %d",DSET_NVALS(dsortar->ar[dd]),      nvals) ;
         ININFO_message("      nx[dsort]=%d  nx[input] = %d",DSET_NX(dsortar->ar[dd]),DSET_NX(inset)) ;
         ININFO_message("      ny[dsort]=%d  ny[input] = %d",DSET_NY(dsortar->ar[dd]),DSET_NY(inset)) ;
         ININFO_message("      nz[dsort]=%d  nz[input] = %d",DSET_NZ(dsortar->ar[dd]),DSET_NZ(inset)) ;
         nbad++ ;
       }
     }
     if( nbad > 0 )
       ERROR_exit("Cannot continue after -dsort mismatch error%s" , (nbad==1)?"\0":"s" ) ;
     num_dsort = dsortar->num ;
   }

#ifdef USE_OMP
  omp_set_nested(0) ;
  maxthr = omp_get_max_threads() ;
   if( maxthr > 1 ){  /* disable threads for some options [16 Jun 2009] */
     if( usetemp ){
       maxthr = 1 ;
       WARNING_message("-usetemp disables OpenMP multi-CPU usage") ;
     } else if( nvox < maxthr*33 ){
       maxthr = 1 ;
       WARNING_message("only %d voxels: disables OpenMP multi-CPU usage in voxel loop",
                       nvox) ;
     }
   }
#if 0
   if( maxthr > 1 ){
     WARNING_message("OpenMP threads disabled at this time -- sorry") ;
     maxthr = 1 ;
   }
#else
   if( maxthr > 1 ) INFO_message("Number of OpenMP threads = %d",maxthr) ;
#endif
#endif

#if 0
   if( nelmat == NULL ) ERROR_exit("No -matrix file?!") ;
#else
   if( nelmat == NULL ){  /* make up a matrix [14 Apr 2009] */
     char *str = NULL ; NI_stream ns ;

     if( matim != NULL ){  /* get matrix from an image */
       int nx = matim->nx, ny = matim->ny; float *far = MRI_FLOAT_PTR(matim);

       if( nx != nvals ) ERROR_exit("-matim nx=%d but nvals=%d",nx,nvals) ;
       if( ny >= nx    ) ERROR_exit("-matim nx=%d but ny=%d"   ,nx,ny) ;
       WARNING_message("No -matrix file: using -matim to create %d x %d matrix",
                       nx , ny ) ;
       str = THD_zzprintf(str,
                          "str:<matrix ni_type='%d*double' ni_dimen='%d'\n"
                          "            NRowFull='%d' GoodList='0..%d' >\n",
                          ny , nvals , nvals , nvals-1 ) ;
       for( ii=0 ; ii < nvals ; ii++ ){
         for( jj=0 ; jj < ny ; jj++ )
           str = THD_zzprintf(str," %g",far[ii+jj*nx]) ;
         str = THD_zzprintf(str,"\n") ;
       }
       str = THD_zzprintf(str,"</matrix>\n") ;

     } else {              /* just make up some crapola */
       double fac = 2.0/(nvals-1.0) ; int kk ;

       polort = MAX(polort,0) ;  /* polynomial matrix [11 Mar 2010] */
       if( polort == 0 )
         WARNING_message(
           "No -matrix file! Making up a matrix with one column of %d 1s",nvals) ;
       else
         WARNING_message(
           "No -matrix file! Making up a matrix %d polynomial columns of length",
           polort+1,nvals) ;
       str = THD_zzprintf(str,
                          "str:<matrix ni_type='%d*double' ni_dimen='%d'\n"
                          "            NRowFull='%d' GoodList='0..%d' >\n",
                          polort+1 , nvals , nvals , nvals-1 ) ;
       for( ii=0 ; ii < nvals ; ii++ ){
         for( kk=0 ; kk <= polort ; kk++ )
           str = THD_zzprintf(str," %g",Plegendre(fac*ii-1.0,kk)) ;
         str = THD_zzprintf(str,"\n") ;
       }
       str = THD_zzprintf(str,"</matrix>\n") ;
     }

     /* convert str to nelmat */

     ns  = NI_stream_open( str , "r" ) ;
     if( ns == (NI_stream)NULL ) ERROR_exit("Cannot fabricate matrix!?") ;
     nelmat = NI_read_element( ns , 9 ) ;
     NI_stream_close(ns) ; free(str) ;
     if( nelmat == NULL ) ERROR_exit("Cannot fabricate matrix?!") ; /* should not happen */
   }
#endif

   usetemp_rcol = usetemp ;
   if( usetemp ){
     if( abfixed || nfile_slibase == 0 || nz == 1 ) usetemp_rcol = 0 ;
     INFO_message(
         "-usetemp filenames will be of the form\n"
         "       %s/REML_%s*\n"
         "   If 3dREMLfit crashes, you may have to 'rm' these manually" ,
         mri_purge_get_tmpdir() , mri_purge_get_tsuf() ) ;
   } else if( nfile_slibase > 0 && nz > 1 ){
     INFO_message(
       "If program runs out of memory (or is 'killed'), try re-running with -usetemp option");
   }

   if( eglt_num == 0 && (Rglt_prefix != NULL || Oglt_prefix != NULL) ){
     WARNING_message("-Rglt/-Oglt disabled since no GLTs on 3dREMLfit command line") ;
     Rglt_prefix = Oglt_prefix = NULL ;
   }

   do_buckt = (Rbuckt_prefix != NULL) || (Obuckt_prefix != NULL) ;
   do_eglt  = (Rglt_prefix   != NULL) || (Oglt_prefix   != NULL) ;

   if( !do_buckt && !do_eglt ){
     if( do_fstat ){
       WARNING_message("-fout disabled because no bucket dataset will be output");
       do_fstat = 0 ;
     }
     if( do_tstat ){
       WARNING_message("-tout disabled because no bucket dataset will be output");
       do_tstat = 0 ;
     }
     if( do_rstat ){
       WARNING_message("-rout disabled because no bucket dataset will be output");
       do_rstat = 0 ;
     }
   }

   if( (do_buckt || do_eglt) && !do_fstat && !do_tstat && !do_rstat ){
     do_fstat = 1 ;
     if( verb )
       INFO_message("assuming -fout since you asked for a bucket dataset") ;
   }

   do_stat = (do_fstat || do_tstat || do_rstat) ;

   /*--------- check out and read the -ABfile, if there is one ---------*/

   if( abset != NULL ){
     float abot,atop , bbot,btop ; ATR_float *atr ; double atm ;

     if( DSET_NX(abset) != nx || DSET_NY(abset) != ny || DSET_NZ(abset) != nz )
       ERROR_exit("-input and -ABfile datasets don't match in grid sizes!") ;
     if( DSET_NVALS(abset) < 2 )
       ERROR_exit("-ABfile must have (at least) 2 sub-bricks!") ;
     else if( DSET_NVALS(abset) > 2 )
       INFO_message("-ABfile has %d sub-bricks: only using first 2" ,
                    DSET_NVALS(abset) ) ;
     if( DSET_BRICK_TYPE(abset,0) != MRI_float ||
         DSET_BRICK_TYPE(abset,1) != MRI_float   )
       ERROR_exit("-ABfile sub-bricks are not stored as floats!?") ;

     DSET_load(abset) ; CHECK_LOAD_ERROR(abset) ;
     aim = DSET_BRICK(abset,0); abot = mri_min(aim); atop = mri_max(aim);
     bim = DSET_BRICK(abset,1); bbot = mri_min(bim); btop = mri_max(bim);
     if( abot < -0.9f || atop > 0.9f || bbot < -0.9f || btop > 0.9f )
       ERROR_exit("-ABfile (a,b) values out of range -0.9..+0.9 ?!") ;

     REML_allow_negative_correlations(1) ; /* who knows what's in -ABfile? */
     if( do_mfilt ){
       do_mfilt = 0 ; WARNING_message("-ABfile disables -Mfilt !!") ;
     }

     atop = fabsf(atop) ; abot = fabsf(abot) ; rhomax = MAX(abot,atop) ;
     btop = fabsf(btop) ; bbot = fabsf(bbot) ; bmax   = MAX(bbot,btop) ;
     if( rhomax < 0.1    ) rhomax = 0.1 ;
     if( bmax   < 0.1    ) bmax   = 0.1 ;
     if( rhomax < rm_set ) rhomax = rm_set ;
     if( bmax   < bm_set ) bmax   = bm_set ;
     atr = THD_find_float_atr( abset->dblk , "REMLFIT_abmax" ) ;
     if( atr != NULL && atr->nfl >= 3 ){
       atm = (double)atr->fl[0] ; if( atm > rhomax ) rhomax = atm ;
       atm = (double)atr->fl[1] ; if( atm > bmax   ) bmax   = atm ;
       ii  = (int)  atr->fl[2] ; if( ii  > nlevab ) nlevab = ii  ;
     }
     rhomax *= 1.00001 ; bmax *= 1.00001 ;
     if( verb )
       INFO_message(
         "-ABfile: (a,b) Grid = %+.3f..%+.3f x %+.3f..%+.3f Levels = %d",
         -rhomax,rhomax , -bmax,bmax , nlevab ) ;

     aim->dx = dx ; aim->dy = dy ; aim->dz = dz ; aar = MRI_FLOAT_PTR(aim) ;
     bim->dx = dx ; bim->dy = dy ; bim->dz = dz ; bar = MRI_FLOAT_PTR(bim) ;

     for( ii=2 ; ii < DSET_NVALS(abset) ; ii++ ) DSET_unload_one(abset,ii) ;

   } else if( abfixed ){

     REML_allow_negative_correlations(1) ; /* anything goes */
     if( do_mfilt ){
       do_mfilt = 0 ; WARNING_message("-ABfile disables -Mfilt") ;
     }

   }

   /*-------------- process the mask somehow --------------*/

   if( statmask != NULL ){               /* 15 Jul 2010 */
     if( statmask->nar != nvox ){
       WARNING_message("-STATmask ignored: doesn't match -input dataset size!") ;
       KILL_bytevec(statmask) ; free(statmask_name) ; statmask_name = NULL ;
     } else {
       int mc ;
       gmask = statmask->ar ; mc = THD_countmask( nvox , gmask ) ;
       if( mc <= 99 ){
         gmask = NULL ;
         KILL_bytevec(statmask) ; free(statmask_name) ; statmask_name = NULL ;
         WARNING_message("-STATmask ignored: only has %d nonzero voxels",mc) ;
       } else if( verb )
         INFO_message("-STATmask has %d voxels (out of %d = %.1f%%)",
                      mc, nvox, (100.0f*mc)/nvox ) ;
     }
   }

   /*-- meanwhile, back at the masking ranch --*/

   if( mask != NULL ){     /* check -mask option for compatibility */

     if( gmask == NULL ) gmask = mask ;
     if( mask_nx != nx || mask_ny != ny || mask_nz != nz )
       ERROR_exit("-mask dataset grid doesn't match input dataset :-(") ;

   } else if( automask ){  /* create a mask from input dataset */

     mask = THD_automask( inset ) ;
     if( mask == NULL )
       ERROR_message("Cannot create -automask from input dataset :-(") ;
     nmask = THD_countmask( nvox , mask ) ;
     if( verb || nmask < 1 )
       INFO_message("Number of voxels in automask = %d (out of %d = %.1f%%)",
                    nmask, nvox, (100.0f*nmask)/nvox ) ;
     if( nmask < 1 ) ERROR_exit("Automask is too small to process") ;
     if( gmask == NULL ) gmask = mask ;

   } else {                /* create a 'mask' for all voxels */

     if( verb )
       INFO_message("No mask ==> computing for all %d voxels",nvox) ;
     mask = (byte *)malloc(sizeof(byte)*nvox) ; nmask = nvox ;
     memset( mask , 1 , sizeof(byte)*nvox ) ;

     if( gmask == NULL && do_FDR && DSET_NX(inset) > 15 &&
                                    DSET_NY(inset) > 15 &&
                                    DSET_NZ(inset) > 15   ){  /* 27 Mar 2009 */
       MRI_IMAGE *qim ; int mc ;
       qim   = THD_rms_brick(inset) ;
       gmask = mri_automask_image( qim ) ;
       mri_free( qim ) ;
       mc = THD_countmask( nvox , gmask ) ;
       if( mc <= 99 && gmask != NULL ){ free(gmask) ; gmask = NULL ; }
       else if( verb )
         INFO_message("FDR automask has %d voxels (out of %d = %.1f%%)",
                      mc, nvox, (100.0f*mc)/nvox ) ;
     }

   }

   mri_fdr_setmask(gmask) ;  /* 27 Mar 2009 */

   /**-------------------- process the matrix --------------------**/

STATUS("process matrix") ;

   nrego  = nelmat->vec_num ;  /* number of matrix columns */
   nrega  = nrego ;            /* 'o'=original 'a'=after -addbase */
   nregda = nrega ;            /* 'da' = after -dsort AND -addbase */
   ntime  = nelmat->vec_len ;  /* number of matrix rows */
   ddof   = ntime - nrego ;
   if( ddof < 1 )              /* should not happen! */
     ERROR_exit("matrix has more columns (%d) than rows (%d) :(",nrego,ntime) ;
   if( ntime < 9 )
     ERROR_exit("matrix has %d rows (time points), but minimum allowed is 9 :-(",ntime) ;

   cgl = NI_get_attribute_nocase( nelmat , "CommandLine" ) ;
   if( cgl != NULL ) commandline = strdup(cgl) ;

   /* warning message if problem is big [05 Jul 2019] */

   if( !usetemp && nfile_slibase == 0 && ntime > 6666 && nmask > 9999 )
     INFO_message(
       "If program runs out of memory (or is 'killed'), try re-running with -usetemp option");

   /*--- number of rows in the full matrix (without censoring) ---*/

   cgl = NI_get_attribute_nocase( nelmat , "NRowFull" ) ;
   if( cgl == NULL ) ERROR_exit("Matrix is missing 'NRowFull' attribute!") ;
   nfull = (int)strtod(cgl,NULL) ;
   if( nvals != nfull )
     ERROR_exit("-input dataset has %d time points, but matrix NRowFull indicates %d",
                nvals , nfull ) ;

   /*--- the goodlist = mapping from matrix row index to time index
                        (which allows for possible time point censoring) ---*/

   cgl = NI_get_attribute_nocase( nelmat , "GoodList" ) ;
   if( cgl == NULL ) ERROR_exit("Matrix is missing 'GoodList' attribute!") ;
   giar = NI_decode_int_list( cgl , ";," ) ;
   if( giar == NULL )
     ERROR_exit("Matrix 'GoodList' badly formatted") ;
   Ngoodlist = giar->num ; goodlist = giar->ar ;
   if( Ngoodlist != ntime )
     ERROR_exit("Matrix 'GoodList' incorrect length: has %d but should be %d",Ngoodlist,ntime) ;

   /*----- run starting points in time indexes -----*/

   rst = NI_get_attribute_nocase( nelmat , "RunStart" ) ;
   if( rst != NULL ){
     NI_int_array *riar = NI_decode_int_list( rst , ";,") ;
     if( riar == NULL ) ERROR_exit("Matrix 'RunStart' badly formatted?") ;
     Nruns = riar->num ; runs = riar->ar ;
   } else {
     if( verb )
       INFO_message("Matrix missing 'RunStart' attribute ==> assuming 1 run = no time discontinuities");
     Nruns = 1 ; runs = calloc(sizeof(int),1) ;
   }
   if( Nruns == 1 ){  /* 21 Jan 2020 */
     min_run = max_run = ntime ;
   } else {
     min_run = 666666 ; max_run = -666666 ;
     for( jj=1 ; jj < Nruns ; jj++ ){
       rnum = runs[jj] - runs[jj-1] ;
       if( min_run > rnum ) min_run = rnum ;
       if( max_run < rnum ) max_run = rnum ;
     }
     rnum = nfull - runs[Nruns-1] ;
     if( min_run > rnum ) min_run = rnum ;
     if( max_run < rnum ) max_run = rnum ;
     INFO_message("shortest run = %d   longest run = %d",min_run,max_run) ;
     if( min_run > 555555 || min_run < 6 || max_run < 6 )
       ERROR_exit("Matrix attribute 'RunStart' has bad values") ;
   }

   /*----- set up pseudo-time tau[] vector for R matrix formation -----*/

   rnum = 0 ; tau = (int *)malloc(sizeof(int)*ntime) ;
   for( ii=0 ; ii < ntime ; ii++ ){
     jj = goodlist[ii] ;        /* time index of the ii-th matrix row */
                              /* then find which run this point is in */
     for( ; rnum+1 < Nruns && jj >= runs[rnum+1] ; rnum++ ) ;   /*nada*/
     tau[ii] = jj + 66666*rnum ;  /* the 66666 means 'very far apart' */
   }

   /*--- re-create the regression matrix X, from the NIML data element ---*/

STATUS("re-create matrix from NIML element") ;

   matrix_initialize( &X ) ;
   matrix_create( ntime , nrego , &X ) ;
   if( nelmat->vec_typ[0] == NI_FLOAT ){        /* from 3dDeconvolve_f */
     float *cd ;
     for( jj=0 ; jj < nrego ; jj++ ){
       cd = (float *)nelmat->vec[jj] ;
       for( ii=0 ; ii < ntime ; ii++ ) X.elts[ii][jj] = (double)cd[ii] ;
     }
   } else if( nelmat->vec_typ[0] == NI_DOUBLE ){  /* from 3dDeconvolve */
     double *cd ;
     for( jj=0 ; jj < nrego ; jj++ ){
       cd = (double *)nelmat->vec[jj] ;
       for( ii=0 ; ii < ntime ; ii++ ) X.elts[ii][jj] = (double)cd[ii] ;
     }
   } else {
     ERROR_exit("Matrix file stored with illegal data type (not float or double)!?") ;
   }

   /*--------------- get column labels for the betas ---------------*/

   cgl = NI_get_attribute_nocase( nelmat , "ColumnLabels" ) ;
   if( cgl == NULL ){
     WARNING_message("ColumnLabels attribute in matrix is missing!?") ;
   } else {
     gsar = NI_decode_string_list( cgl , ";" ) ;
     if( gsar == NULL )
       ERROR_exit("ColumnLabels attribute in matrix is malformed!?") ;
     if( gsar->num < nrego )
       ERROR_exit("ColumnLabels attribute has only %d labels but matrix has %d columns",gsar->num,nrego) ;
     beta_lab = gsar->str ;
   }

   /****------------------ process -addbase images ------------------****/

   if( imar_addbase != NULL ){
     MRI_IMAGE *im ; int pp ; float *iar ;

STATUS("process -addbase images") ;

     nrega += ncol_addbase ;  /* new number of regressors */

     if( verb )
       INFO_message("Adding %d column%s to X matrix via '-addbase'" ,
                    ncol_addbase , (ncol_addbase==1) ? "" : "s"      ) ;

     /*--- check each image for OK-ness == right length in time ---*/

     for( nbad=ii=0 ; ii < IMARR_COUNT(imar_addbase) ; ii++ ){
       im = IMARR_SUBIM( imar_addbase , ii ) ;
       if( im->nx == ntime ) continue ; /* OK */
       if( im->nx == nfull ){           /* must be censored */
         MRI_IMAGE *imb = mri_subset_x2D( ntime , goodlist , im ) ;
         mri_free(im) ; IMARR_SUBIM(imar_addbase,ii) = imb ;
         if( verb )
           INFO_message("Censored -addbase file '%s' from %d down to %d rows" ,
                        imb->name , nfull , ntime ) ;
         continue ;
       }
       ERROR_message("-addbase file '%s' has %d rows, but matrix has %d !?" ,
                     im->name , im->nx , ntime ) ;
       nbad++ ;
     }

     ddof = ntime - nrega ;
     if( ddof < 1 ){
       ERROR_exit("matrix has more columns (%d) than rows (%d) after -addbase :(" ,
                  nrega , ntime ) ;
       nbad++ ;
     }

     if( nbad > 0 ) ERROR_exit("Cannot continue after -addbase errors!") ;

     /*------ make up extra labels for these columns ------*/

     if( beta_lab != NULL ){
       char lll[64] ;
       beta_lab = NI_realloc( beta_lab , char * , sizeof(char *)*nrega ) ;
       for( kk=nrego,ii=0 ; ii < IMARR_COUNT(imar_addbase) ; ii++ ){
         im = IMARR_SUBIM( imar_addbase , ii ) ;
         for( jj=0 ; jj < im->ny ; jj++ ){
           if( im->ny > 1 ) sprintf(lll,"%.48s[%d]",im->name,jj) ;
           else             sprintf(lll,"%.48s"    ,im->name   ) ;
           beta_lab[kk++] = NI_strdup(lll) ;
         }
       }
     }

     /*--- enlarge the matrix, add the new columns to it, and proceed ---*/

     matrix_enlarge( 0 , ncol_addbase , &X ) ;
     for( kk=nrego,ii=0 ; ii < IMARR_COUNT(imar_addbase) ; ii++ ){
       im = IMARR_SUBIM(imar_addbase,ii) ; iar = MRI_FLOAT_PTR(im) ;
       for( jj=0 ; jj < im->ny ; jj++,kk++,iar+=im->nx ){  /* kk = matrix col */
         if( dmbase ){       /* demean the column? */      /* jj = input col */
           float csum=0.0f ;
           for( pp=0 ; pp < ntime ; pp++ ) csum += iar[pp] ;
           csum /= ntime ;
           for( pp=0 ; pp < ntime ; pp++ ) iar[pp] -= csum ;
         }
         for( pp=0 ; pp < ntime ; pp++ ) X.elts[pp][kk] = (double)iar[pp] ;
       }
     }

     DESTROY_IMARR(imar_addbase) ;

   } /**** end of -addbase stuff ****/

#if 0
{ double *sv = matrix_singvals(X) ;
  fprintf(stderr,"X singular values:") ;
  for( ii=0 ; ii < X.cols ; ii++ ) fprintf(stderr," %g",sv[ii]) ;
  fprintf(stderr,"\n") ; free(sv) ;
}
#endif

   /**--------------- check matrix for all zero columns ---------------**/

   for( nbad=jj=0 ; jj < nrega ; jj++ ){
     for( ii=0 ; ii < ntime && X.elts[ii][jj]==0.0 ; ii++ ) ; /*nada*/
     if( ii==ntime ){
       ERROR_message("matrix column #%d is all zero!?",jj) ; nbad++ ;
       if( allz == NULL ) allz = (int *)malloc(sizeof(int)*nrega) ;
       allz[nallz++] = jj ;
     }
   }
   if( nbad > 0 ){
     if( goforit ){
       WARNING_message("You said to GOFORIT, so here we GO!") ;
       ININFO_message(
         "         Note that a bunch of further WARNINGs will be generated below.") ;
       allz = (int *)realloc(allz,sizeof(int)*nallz) ;
     } else
       ERROR_exit(
        "Cannot continue with all zero column%s without -GOFORIT option!",
        (nbad==1) ? "\0" : "s" ) ;
   }

   /****------------------ process -slibase images ------------------****/

   if( imar_slibase == NULL ){  /** no per-slice regressors */

     Xsli    = (matrix **)malloc(sizeof(matrix *)) ;
     Xsli[0] = &X ;       /* matrix for this "slice" */
     nsli    = 1 ;        /* number of slices */
     nsliper = (1<<30) ;  /* number of voxels per slice = 1 beellion */

   } else {                     /** have per-slice regressors **/

     MRI_IMAGE *im ; int pp,nregq,nc,cc ; float *iar ; matrix *Xs ;

STATUS("process -slibase images") ;

     /* figure out how many columns will be added */

     nsli = nz ; ncol_slibase = 0 ;

     for( nbad=ii=0 ; ii < IMARR_COUNT(imar_slibase) ; ii++ ){
       im = IMARR_SUBIM( imar_slibase , ii ) ;
       nc = im->ny / nsli ;  /* how many slice sets are in this image */
       kk = im->ny % nsli ;  /* how many left over (should be zero) */
       if( kk != 0 ){
         ERROR_message(
           "-slibase file '%s' has %d columns but dataset has %d slices",
           im->name , im->ny , nsli ) ;
         nbad++ ; continue ;
       }
       ncol_slibase += nc ;  /* total number of columns to add to matrix */
       if( im->nx == ntime ) continue ; /* OK */
       if( im->nx == nfull ){
         MRI_IMAGE *imb = mri_subset_x2D( ntime , goodlist , im ) ;
         mri_free(im) ; IMARR_SUBIM(imar_slibase,ii) = imb ;
         if( verb )
           INFO_message("Censored -slibase file '%s' from %d down to %d rows" ,
                        imb->name , nfull , ntime ) ;
         continue ;
       }
       ERROR_message("-slibase file '%s' has %d rows, but matrix has %d" ,
                     im->name , im->nx , ntime ) ;
       nbad++ ;
     }

     nregq   = nrega ;         /* save this for a little bit */
     nrega  += ncol_slibase ;  /* new number of regressors */
     nsliper = nxy ;           /** number of voxels per slice **/

     if( verb )
       INFO_message("Adding %d column%s to X matrix via '-slibase'" ,
                    ncol_slibase , (ncol_slibase==1) ? "" : "s"      ) ;

     ddof = ntime - nrega ;
     if( ddof < 1 ){
       ERROR_message("matrix has more columns (%d) than rows (%d) after -slibase!" ,
                     nrega , ntime ) ;
       nbad++ ;
     }

     if( nbad > 0 ) ERROR_exit("Cannot continue after -slibase errors!") ;

     /*--------- make up extra labels for these columns ---------*/

     if( beta_lab != NULL ){
       char lll[32] ;
       beta_lab = NI_realloc( beta_lab , char * , sizeof(char *)*nrega ) ;
       for( kk=nregq,ii=0 ; ii < ncol_slibase ; ii++,kk++ ){
         sprintf(lll,"slibase#%d",ii+1) ;
         beta_lab[kk] = NI_strdup(lll) ;
       }
     }

     /*------ for each slice, make up a new matrix, add columns to it ------*/

     Xsli = (matrix **)malloc(sizeof(matrix *)*nsli) ;  /* array of matrices */

     /* loop over slice indexes */

     for( nbad=ss=0 ; ss < nsli ; ss++ ){       /* ss = slice index */

       Xs = (matrix *)malloc(sizeof(matrix)) ;  /* new matrix */
       matrix_initialize(Xs) ;
       matrix_equate( X , Xs ) ;                /* copy in existing matrix */
       matrix_enlarge( 0,ncol_slibase , Xs ) ;  /* make new one bigger */

       /* loop over slibase image files:
           ii = image file index
           kk = column index in matrix to get new data
           nc = number of slices sets in ii-th image (nsli columns per set)
           cc = loop index over slice sets (skipping nsli columns in image) */

       for( kk=nregq,ii=0 ; ii < IMARR_COUNT(imar_slibase) ; ii++ ){

         im  = IMARR_SUBIM(imar_slibase,ii) ;   /* ii-th slibase image */
         nc  = im->ny / nsli ;
         iar = MRI_FLOAT_PTR(im) ;  /* pointer to data */
         iar += ss * im->nx ;       /* pointer to ss-th column */

         /* loop over the ss-th slice set in this file:
             skip head nsli columns in iar at each iteration */

         for( cc=0 ; cc < nc ; cc++,kk++,iar+=nsli*im->nx ){
           if( dmbase ){        /* de-mean the column */
             float csum=0.0f ;
             for( pp=0 ; pp < ntime ; pp++ ) csum += iar[pp] ;
             csum /= ntime ;
             for( pp=0 ; pp < ntime ; pp++ ) iar[pp] -= csum ;
           }
           for( pp=0 ; pp < ntime ; pp++ ) Xs->elts[pp][kk] = (double)iar[pp] ;

           for( pp=0 ; pp < ntime && iar[pp]==0.0f ; pp++ ) ; /*nada*/
           if( pp == ntime ){
             ERROR_message("-slibase file %s col #%d is all zero",im->name,ss) ;
             nbad++ ;
           }
         } /* end of loop over cc = slice set */

       } /* end of loop over input files, extracting for the ss-th slice */

       Xsli[ss] = Xs ;  /* save this completed matrix in the array */

     } /* end of loop over slices */

     if( nbad > 0 ) ERROR_exit("Cannot continue after -slibase errors!") ;

     DESTROY_IMARR(imar_slibase) ;

   } /**** end of -slibase stuff ****/

   /** allow space for -dsort betas **/

   if( num_dsort > 0 ){
     nregda = nrega + num_dsort ;

     ddof = ntime - nregda ;
     if( ddof < 1 )
       ERROR_exit("matrix has more columns (%d) than rows (%d) after -dsort!" ,
                  nregda , ntime ) ;

     if( beta_lab != NULL ){
       char lll[32] ;
       beta_lab = NI_realloc( beta_lab , char * , sizeof(char *)*nregda ) ;
       for( kk=nrega,ii=0 ; ii < num_dsort ; ii++,kk++ ){
         sprintf(lll,"dsort#%d",ii+1) ;
         beta_lab[kk] = NI_strdup(lll) ;
       }
     }
   } else {
     nregda = nrega ;
   }

   /** Modify denominator DOF? Don't subtract for all zero regressors [16 Mar 2010] **/

   ddof += nallz ;
   if( nallz > 0 && verb > 1 )
     INFO_message(
      "Denominator DOF increased from %d to %d to allow for all zero columns" ,
      ddof-nallz , ddof ) ;

   /**---- check X matrices for collinearity ----**/

   { char lab[32]="\0" ; int nkill ;
     for( nbad=ss=0 ; ss < nsli ; ss++ ){
       if( nsli > 1 ) sprintf(lab,"slice #%d",ss) ;
       nbad += check_matrix_condition( *(Xsli[ss]) , lab ) ;
     }
     if( nbad > 0 ) {
       if( !goforit ){
         ERROR_exit("Cannot continue after matrix condition errors!\n"
                    "** you might try -GOFORIT, but be careful! (cf. '-help')");
       }

       WARNING_message("-GOFORIT ==> Charging ahead into battle!") ;
       ININFO_message ("                  ==> Check results carefully!") ;
     }
   }

   /***---------- extract stim information from matrix header ----------***/

   cgl = NI_get_attribute_nocase( nelmat , "Nstim" ) ;
   if( cgl != NULL ){

     stim_num = (int)strtod(cgl,NULL) ;
     if( stim_num <= 0 ) ERROR_exit("Nstim attribute in matrix is not positive!");

     cgl = NI_get_attribute_nocase( nelmat , "StimBots" ) ;
     if( cgl == NULL ) ERROR_exit("Matrix is missing 'StimBots' attribute!") ;
     giar = NI_decode_int_list( cgl , ";," ) ;
     if( giar == NULL )
       ERROR_exit("Matrix 'StimBots' badly formatted?!") ;
     if( giar->num < stim_num )
       ERROR_exit("Matrix 'StimBots' has only %d values but needs %d",giar->num,stim_num) ;
     stim_bot = giar->ar ;

     cgl = NI_get_attribute_nocase( nelmat , "StimTops" ) ;
     if( cgl == NULL ) ERROR_exit("Matrix is missing 'StimTops' attribute!") ;
     giar = NI_decode_int_list( cgl , ";," ) ;
     if( giar == NULL )
       ERROR_exit("Matrix 'StimTops' badly formatted?!") ;
     if( giar->num < stim_num )
       ERROR_exit("Matrix 'StimTops' has only %d values but needs %d",giar->num,stim_num) ;
     stim_top = giar->ar ;

     cgl = NI_get_attribute_nocase( nelmat , "StimLabels" ) ;
     if( cgl == NULL ) ERROR_exit("Matrix is missing 'StimLabels' attribute!");
     gsar = NI_decode_string_list( cgl , ";" ) ;
     if( gsar == NULL )
       ERROR_exit("Matrix 'StimLabels' badly formatted?!") ;
     if(  gsar->num < stim_num )
       ERROR_exit("Matrix 'StimLabels' has only %d values but needs %d",giar->num,stim_num) ;
     stim_lab = gsar->str ;

     nSymStim = stim_num+2 ;
     SymStim  = (SYM_irange *)calloc(sizeof(SYM_irange),nSymStim) ;

     strcpy( SymStim[stim_num].name , "Ort" ) ;    /* pre-stim baselines */
     SymStim[stim_num].nbot = 0 ;
     SymStim[stim_num].ntop = stim_bot[0] - 1 ;
     SymStim[stim_num].gbot = 0 ;

     strcpy( SymStim[stim_num+1].name , "Col" ) ;  /* all regressors */
     SymStim[stim_num+1].nbot = 0 ;
     SymStim[stim_num+1].ntop = nrega-1 ;
     SymStim[stim_num+1].gbot = 0 ;

#if 0
     if( num_dsort > 0 ){
       nSymStim++ ;
       SymStim = (SYM_irange *)realloc(SymStim,sizeof(SYM_irange)*nSymStim) ;
       strcpy( SymStim[stim_num+2].name , "Dsort" ) ;
       SymStim[stim_num+2].nbot = nrega ;
       SymStim[stim_num+2].ntop = nregda-1 ;
       SymStim[stim_num+2].gbot = 0 ;
     }
#endif

     for( ss=0 ; ss < stim_num ; ss++ ){  /* "normal" stimulus regressors */
       SymStim[ss].nbot = 0 ;
       SymStim[ss].ntop = stim_top[ss]-stim_bot[ss] ;
       SymStim[ss].gbot = stim_bot[ss] ;
       MCW_strncpy( SymStim[ss].name , stim_lab[ss] , 64 ) ;
     }

   } else {  /* don't have stim info in matrix header?! */

     WARNING_message("-matrix file is missing Stim attributes:") ;
     ININFO_message ("can only use symbolic name 'Col' in GLTs (cf. '-help')" ) ;
     if( do_buckt )
       WARNING_message(" ==> bucket dataset output is disabled") ;
     if( nobout )
       WARNING_message(" ==> -nobout is disabled") ;
     nobout = do_buckt = do_glt = 0 ; Rbuckt_prefix = Obuckt_prefix = NULL ;

     /* a fake name */

     nSymStim = 1 ;
     SymStim  = (SYM_irange *)calloc(sizeof(SYM_irange),nSymStim) ;
     strcpy( SymStim[0].name , "Col" ) ;
     SymStim[0].nbot = 0 ;
     SymStim[0].ntop = nrega-1 ;
     SymStim[0].gbot = 0 ;

#if 0
     if( num_dsort > 0 ){
       nSymStim++ ;
       SymStim = (SYM_irange *)realloc(SymStim,sizeof(SYM_irange)*nSymStim) ;
       strcpy( SymStim[1].name , "Dsort" ) ;  /* dsort regressors */
       SymStim[1].nbot = nrega ;
       SymStim[1].ntop = nregda-1 ;
       SymStim[1].gbot = 0 ;
     }
#endif

   }

   /***--- set the betas to keep in the Rbeta/Obeta outputs ---***/

   betaset = (int *)malloc(sizeof(int)*nregda) ;  /* beta indexes to keep */

   if( !nobout ){
     for( ii=0 ; ii < nregda ; ii++ ) betaset[ii] = ii ;
     nbetaset = nregda ;
   } else {
     for( kk=jj=0 ; jj < stim_num ; jj++ ){
       for( ii=stim_bot[jj] ; ii <= stim_top[jj] ; ii++ ) betaset[kk++] = ii ;
     }
     nbetaset = kk ;
   }

   /***--- setup to do statistics on the stimuli betas, if desired ---***/

#define SKIP_SMAT 1
#undef  ADD_GLT
#define ADD_GLT(lb,gg)                                                              \
 do{ glt_lab = (char **   )realloc((void *)glt_lab, sizeof(char *   )*(glt_num+1)); \
     glt_mat = (matrix ** )realloc((void *)glt_mat, sizeof(matrix * )*(glt_num+1)); \
     glt_smat= (sparmat **)realloc((void *)glt_smat,sizeof(sparmat *)*(glt_num+1)); \
     glt_lab[glt_num] = strdup(lb) ; glt_mat[glt_num] = (gg) ;                      \
     glt_smat[glt_num]= (SKIP_SMAT) ? NULL : matrix_to_sparmat(*gg) ;               \
     glt_num++ ; glt_rtot += gg->rows ;                                             \
 } while(0)

#undef  KILL_ALL_GLTS
#define KILL_ALL_GLTS                                      \
 do{ for( ii=0 ; ii < glt_num ; ii++ ){                    \
       free(glt_lab[ii]) ; sparmat_destroy(glt_smat[ii]) ; \
       matrix_destroy(glt_mat[ii]) ; free(glt_mat[ii]) ;   \
     }                                                     \
     free(glt_lab); free(glt_mat); free(glt_smat);         \
 } while(0)

   if( do_stat && do_glt ){
     char lnam[32] ; int nn,mm ; matrix *gm ;
     int *set = (int *)malloc(sizeof(int)*nregda) ;  /* list of columns to keep */

STATUS("make stim GLTs") ;

     /*------ first set of indexes is all stimuli ------*/

     for( kk=jj=0 ; jj < stim_num ; jj++ ){
       for( ii=stim_bot[jj] ; ii <= stim_top[jj] ; ii++ ) set[kk++] = ii ;
     }
     gm = create_subset_matrix( nrega , kk , set ) ;
     if( gm == NULL ) ERROR_exit("Cannot create G matrix for FullModel?!") ; /* should never happen */
     ADD_GLT( "Full" , gm ) ;

     allstim = (int *)malloc(sizeof(int)*kk) ; num_allstim = kk ;
     AAmemcpy( allstim , set , sizeof(int)*num_allstim ) ;
     qsort_int( num_allstim , allstim ) ;

     /*--- now do each labeled stimulus separately ---*/

     for( jj=0 ; jj < stim_num ; jj++ ){
       for( kk=0,ii=stim_bot[jj] ; ii <= stim_top[jj] ; ii++ ) set[kk++] = ii ;
       gm = create_subset_matrix( nrega , kk , set ) ;
       if( gm == NULL )
         ERROR_exit("Cannot create G matrix for %s ?!",stim_lab[jj]); /* not sure if this is possible */
       ADD_GLT( stim_lab[jj] , gm ) ;
     }

     free((void *)set) ;  /* not needed no more, no how, no way */
   }

   /*------ extract other GLT information from matrix header ------*/

   cgl = NI_get_attribute_nocase( nelmat , "Nglt" ) ;
   if( do_stat && (cgl != NULL || eglt_num > 0) ){
     char lnam[32],*lll ; int nn,mm,ngl ; matrix *gm ; float *far ;

     /*----- process GLTs from the matrix header -----*/

     if( cgl != NULL && do_glt ){
       static char *glt_format[6] =
                   { "GltMatrix_%06d" , "GltMatrix_%05d" , "GltMatrix_%04d" ,
                     "GltMatrix_%03d" , "GltMatrix_%02d" , "GltMatrix_%d"    } ;
       int dig ;
STATUS("make GLTs from matrix file") ;
       ngl = (int)strtod(cgl,NULL) ;
       if( ngl <= 0 || ngl > 1000000 )
         ERROR_exit("Nglt attribute in matrix = '%s' ??? :(",cgl) ;

       cgl = NI_get_attribute_nocase( nelmat , "GltLabels" ) ;
       if( cgl == NULL ) ERROR_exit("Matrix is missing 'GltLabels' attribute!");
       gsar = NI_decode_string_list( cgl , ";" ) ;
       if( gsar == NULL )
         ERROR_exit("Matrix 'GltLabels' badly formatted?!") ;
       if( gsar->num < ngl )
         ERROR_exit("Matrix 'GltLabels' has %d labels but should have %d",gsar->num,ngl) ;

       for( kk=0 ; kk < ngl ; kk++ ){
         for( cgl=NULL,dig=0 ; cgl==NULL && dig < 6 ; dig++ ){ /* diverse number */
           sprintf(lnam,glt_format[dig],kk) ;                  /* of leading 0s */
           cgl = NI_get_attribute_nocase( nelmat , lnam ) ;
         }
         if( cgl == NULL ) ERROR_exit("Matrix is missing '%s' attribute!",lnam) ;
         gfar = NI_decode_float_list( cgl , "," ) ;
         if( gfar == NULL )
           ERROR_exit("Matrix attribute '%s' is badly formatted?!",lnam) ;
         if( gfar->num < 3 )
           ERROR_exit("Matrix attribute '%s' has only %d values?",lnam,gfar->num) ;
         far = gfar->ar ; nn = (int)far[0] ; mm = (int)far[1] ;
         if( nn <= 0 ) ERROR_exit("GLT '%s' has %d rows?",lnam,nn) ;
         if( mm != nrego )
           ERROR_exit("GLT '%s' has %d columns (should be %d)?",lnam,mm,nrego) ;
         if( gfar->num - 2 < nn*mm )
           ERROR_exit("GLT '%s' has %d rows and %d columns, but only %d matrix entries?",
                      lnam , nn , mm , gfar->num-2 ) ;
         gm = (matrix *)malloc(sizeof(matrix)) ; matrix_initialize(gm) ;
         matrix_create( nn, nrega, gm ) ;
         for( ii=0 ; ii < nn ; ii++ ){
           for( jj=0 ; jj < mm ; jj++ ) gm->elts[ii][jj] = far[jj+2+ii*mm] ;
         }
         lll = gsar->str[kk] ; if( lll == NULL || *lll == '\0' ) lll = lnam ;
         ADD_GLT( lll , gm ) ;
       }
     } /* end of GLTs from the matrix file */

     /**------ now process any extra GLTs on our local command line ------**/

     oglt_num = glt_num ;  /* number of 'original' GLTs */

     if( eglt_num > 0 ) STATUS("make GLTs from command line") ;

     for( nbad=kk=0 ; kk < eglt_num ; kk++ ){
       gm = create_gltsym( eglt_sym[kk] , nrega ) ;
       if( gm != NULL ) ADD_GLT( eglt_lab[kk] , gm ) ;
       else             nbad++ ;
     }
     if( nbad > 0 || SYM_expand_errcount() > 0 )
       ERROR_exit("Cannot continue after -gltsym errors!") ;

   } /* end of GLT setup */

   /**-- if have all zero matrix columns,  [15 Mar 2010]
         set those coefficients to have zero weight in the GLT matrices --**/

   if( nallz > 0 ){
     matrix *G ; int cc ;
     if( verb > 1 )
       INFO_message("Editing GLT matrices for all zero X matrix column%s",
                    (nallz==1) ? "\0" : "s" ) ;
     for( kk=0 ; kk < glt_num ; kk++ ){
       G = glt_mat[kk] ;
       for( jj=0 ; jj < nallz ; jj++ ){
         cc = allz[jj] ;
         for( ii=0 ; ii < G->rows ; ii++ ) G->elts[ii][cc] = 0.0 ;
       }
     }
   }

   /**-- edit out any all zero rows in the GLT matrices [15 Mar 2010] --**/

   { matrix *G , *GM ; int nrk , ndone=0 ;
     GM = (matrix *)malloc(sizeof(matrix)) ; matrix_initialize(GM) ;
     for( kk=0 ; kk < glt_num ; kk++ ){
       G   = glt_mat[kk] ;
       nrk = matrix_delete_allzero_rows( *G , GM ) ;
       if( nrk > 0 && nrk < G->rows ){
         ndone++ ;
         if( verb > 1 )
           ININFO_message("Removed %d all zero row%s from GLT matrix '%s';"
                          " numerator DOF changes from %d to %d",
                          nrk , (nrk==1) ? "\0" : "s" , glt_lab[kk] , G->rows,GM->rows ) ;
         glt_mat[kk] = GM ; matrix_destroy(G) ; glt_rtot -= nrk ;
         GM = (matrix *)malloc(sizeof(matrix)) ; matrix_initialize(GM) ;
       } else if( nrk == G->rows ){
         ININFO_message("GLT matrix '%s' is all zero!",glt_lab[kk]) ;
       }
     }
     free(GM) ;
     if( ndone == 0 && verb > 1 && nallz > 0 )
       ININFO_message("No GLT matrices neededed editing") ;
   }
   if( allz != NULL ){ free(allz) ; allz = NULL ; }

   /***--------- done with nelmat ---------***/

   NI_free_element(nelmat) ;  /* save some memory */

   /**------ load time series dataset, check for all zero voxels ------**/

   if( !DSET_LOADED(inset) ){
     if( verb ) INFO_message("Loading input dataset into memory") ;
     DSET_load(inset) ; CHECK_LOAD_ERROR(inset) ;
     MEMORY_CHECK ;
   }

   vector_initialize( &y ) ; vector_create_noinit( ntime , &y ) ;
   niv = (nvals+nregda+glt_num+99)*2 ;
   iv  = (float *)malloc(sizeof(float)*(niv+1)) ; /* temp vectors */
   jv  = (float *)malloc(sizeof(float)*(niv+1)) ;

   for( nbad=vv=0 ; vv < nvox ; vv++ ){
     if( !INMASK(vv) ) continue ;
     (void)THD_extract_array( vv , inset , 0 , iv ) ;  /* data vector */
     for( ii=0 ; ii < ntime && iv[goodlist[ii]]==0.0f ; ii++ ) ; /*nada*/
     if( ii == ntime ){
       mask[vv] = 0 ; nbad++ ;
       if( gmask != NULL && gmask != mask ) gmask[vv] = 0 ;
     }
   }
   if( nbad > 0 ){
     nmask = THD_countmask( nvox , mask ) ;
     if( verb || nmask < 1 )
       ININFO_message("masked off %d voxel%s for being all zero; %d left in mask" ,
                      nbad , (nbad==1) ? "" : "s" , nmask ) ;
     if( nmask < 1 ) ERROR_exit("Cannot continue after mask shrinks to nothing!") ;
   }

#ifdef USE_OMP
   if( maxthr > 1 && nmask < 33*maxthr ){
     maxthr = 1 ;
     WARNING_message(
      "only %d voxels in mask: disables OpenMP multi-CPU usage for voxel loop",
      nmask) ;
   }
#endif
   if( maxthr == 1 ) virtu_mrv = 0 ;  /* 13 Dec 2012 */

   /* 05 Nov 2008: convert input to a vector image struct, if it saves memory */

   if( virtu_mrv || !AFNI_noenv("AFNI_REML_ALLOW_VECTIM") ){
     double vsiz = (double)THD_vectim_size( inset , mask ) ;
     double dsiz = (double)DSET_TOTALBYTES( inset ) ;
     if( virtu_mrv || (vsiz < 0.9*dsiz && vsiz > 10.0e+6) ){
       if( verb ){
         INFO_message("Converting input dataset to vector image") ;
         ININFO_message(" dataset = %s bytes",approximate_number_string(dsiz)) ;
         ININFO_message(" vectim  = %s bytes",approximate_number_string(vsiz)) ;
       }
       inset_mrv = THD_dset_to_vectim( inset , mask , 0 ) ;
       if( inset_mrv != NULL )  DSET_unload(inset) ;
       else                   { ERROR_message("Cannot create vector image!?"); virtu_mrv = 0; }

       if( inset_mrv != NULL )
         THD_check_vectim(inset_mrv,"3dREMLfit input data") ;

       if( virtu_mrv ){
         fname_mrv = mri_get_tempfilename("JUNK") ;
         ii = THD_vectim_data_tofile( inset_mrv , fname_mrv ) ;
         if( ii == 0 ){
           ERROR_message("Cannot write vector image to temp file %s",fname_mrv) ;
           virtu_mrv = 0 ; free(fname_mrv) ; fname_mrv = NULL ;
         } else {
           free(inset_mrv->fvec) ; inset_mrv->fvec = NULL ;
           ININFO_message(" -virtvec: vector image stored in temp file %s",fname_mrv) ;
         }
       }
       MEMORY_CHECK ;
     }
   }

   /**---------------------- set up for REML estimation ---------------------**/

   if( verb ){
     INFO_message("starting REML setup calculations; total CPU=%.2f Elapsed=%.2f",
                  COX_cpu_time(),COX_clock_time() ) ;
     if( abfixed ) ININFO_message(" using fixed a=%.4f b=%.4f lam=%.4f",
                                  afix,bfix,LAMBDA(afix,bfix) ) ;
   }

   RCsli = (reml_collection **)calloc(sizeof(reml_collection *),nsli) ;

   if( !usetemp_rcol ){  /* set up them all */

     if( verb > 1 ) REMLA_memsetup ;
     for( ss=0 ; ss < nsli ; ss++ ){  /* might take a while */
       if( abfixed )
         rrcol = REML_setup_all( Xsli[ss] , tau , 0     , afix  ,bfix ) ;
       else {
         if( verb && nsli > 1 && ntime*nrega > 9999 )
           ININFO_message(" start setup for slice #%d",ss) ;
         rrcol = REML_setup_all( Xsli[ss] , tau , nlevab, rhomax,bmax ) ;
       }
       if( rrcol == NULL ) ERROR_exit("REML setup fails at ss=%d?!",ss ) ; /* really bad */
       RCsli[ss] = rrcol ;
     }

     if( verb > 1 ){
       float avg=0.0f ;
       ININFO_message(
        "REML setup finished: matrix rows=%d cols=%d; %d*%d cases; total CPU=%.2f Elapsed=%.2f",
        ntime,nrega,RCsli[0]->nset,nsli,COX_cpu_time(),COX_clock_time()) ;
       for( ss=0 ; ss < nsli ; ss++ ) avg += RCsli[ss]->avglen ;
       avg /= nsli ; ININFO_message(" average case bandwidth = %.2f",avg) ;
       if( verb > 1 ) REMLA_memprint ;
     }
     MEMORY_CHECK ;

   } else {  /* just set up the first one (slice #0) */

     rrcol = REML_setup_all( Xsli[0] , tau , nlevab, rhomax,bmax ) ;
     if( rrcol == NULL ) ERROR_exit("REML setup fails?!" ) ; /* really bad */
     RCsli[0] = rrcol ;

     if( verb > 1 )
       ININFO_message(
        "REML setup #0 finished: matrix rows=%d cols=%d; %d cases; total CPU=%.2f Elapsed=%.2f",
        ntime,nrega,RCsli[0]->nset,COX_cpu_time(),COX_clock_time()) ;
     MEMORY_CHECK ;

   }

   izero = RCsli[0]->izero ;  /* index of (a=0,b=0) case */

   /***------------- loop over voxels, find best REML values ------------***/

   vstep = (verb && nvox > 999) ? nvox/50 : 0 ;

   if( aim == NULL && !abfixed ){ /*--- if don't have (a,b) via -ABfile ---*/

     aim = mri_new_vol( nx,ny,nz , MRI_float ) ;
     aim->dx = dx ; aim->dy = dy ; aim->dz = dz ; aar = MRI_FLOAT_PTR(aim) ;
     bim = mri_new_vol( nx,ny,nz , MRI_float ) ;
     bim->dx = dx ; bim->dy = dy ; bim->dz = dz ; bar = MRI_FLOAT_PTR(bim) ;
     rim = mri_new_vol( nx,ny,nz , MRI_float ) ;
     rim->dx = dx ; rim->dy = dy ; rim->dz = dz ; rar = MRI_FLOAT_PTR(rim) ;

     if( vstep ){ fprintf(stderr,"++ REML voxel loop: ") ; vn = 0 ; }

  if( maxthr <= 1 ){   /**--------- serial computation (no threads) ---------**/
    int ss,rv,vv,ssold,ii,kbest ;
    int   na = RCsli[0]->na , nb = RCsli[0]->nb , nab = (na+1)*(nb+1) ;
    int   nws = nab + 7*(2*niv+32) + 32 ;
    double *ws = (double *)malloc(sizeof(double)*nws) ;
#ifdef REML_DEBUG
    char *fff ; FILE *fpp=NULL ;
    fff = getenv("REML_DEBUG") ;
    if( fff != NULL ) fpp = fopen( fff , "w" ) ;
#endif

     for( ss=-1,rv=vv=0 ; vv < nvox ; vv++ ){    /* this will take a long time */
       if( vstep && vv%vstep==vstep-1 ) vstep_print() ;
       if( !INMASK(vv) ) continue ;
       if( inset_mrv != NULL ){ /* 05 Nov 2008 */
         VECTIM_extract( inset_mrv , rv , iv ) ; rv++ ;
       } else
         (void)THD_extract_array( vv , inset , 0 , iv ) ;      /* data vector */
       for( ii=0 ; ii < ntime ; ii++ ) y.elts[ii] = (double)iv[goodlist[ii]] ;
       ssold = ss ; ss = vv / nsliper ;      /* slice index in Xsli and RCsli */
       if( usetemp_rcol && ss > ssold && ssold >= 0 )     /* purge last slice */
         reml_collection_save( RCsli[ssold] ) ;              /* setup to disk */
       if( RCsli[ss] == NULL ){                     /* create this slice now? */
         if( verb > 1 && vstep ) fprintf(stderr,"+") ;
         RCsli[ss] = REML_setup_all( Xsli[ss] , tau , nlevab, rhomax,bmax ) ;
         if( RCsli[ss] == NULL ) ERROR_exit("REML setup fails for ss=%d",ss) ; /* really bad */
       }
       kbest = REML_find_best_case( &y , RCsli[ss] , nws,ws ) ;
       aar[vv] = RCsli[ss]->rs[kbest]->rho ;
       bar[vv] = RCsli[ss]->rs[kbest]->barm ;
       rar[vv] = ws[0] ;

#ifdef REML_DEBUG
       if( fpp != NULL ){
         int qq ;
         fprintf(fpp,"%d ",vv) ;
         fprintf(fpp," y=") ;
         for( qq=0 ; qq < ntime ; qq++ ) fprintf(fpp," %g",y.elts[qq]) ;
         fprintf(fpp,"  R=") ;
         for( qq=1 ; qq <= nab ; qq++ ) fprintf(fpp," %g",ws[qq]) ;
         fprintf(fpp,"\n") ;
       }
#endif

     } /* end of REML loop over voxels */

     free(ws) ;
#ifdef REML_DEBUG
     if( fpp != NULL ) fclose(fpp) ;
#endif

  } else {  /**---------------- Parallelized (not paralyzed) ----------------**/

    int *vvar ;
    int   na = RCsli[0]->na , nb = RCsli[0]->nb , nab = (na+1)*(nb+1) ;
    int   nws = nab + 7*(2*niv+32) + 32 ;
#ifdef REML_DEBUG
    char *fff ; FILE *fpp=NULL ;
    fff = getenv("REML_DEBUG") ;
    if( fff != NULL ) fpp = fopen( fff , "w" ) ;
#endif

    vvar = (int *)malloc(sizeof(int)*nmask) ;
    for( vv=ii=0 ; vv < nvox ; vv++ ) if( INMASK(vv) ) vvar[ii++] = vv ;
    if( vstep ){ fprintf(stderr,"[%d threads]",maxthr) ; vn = 0 ; }

#ifdef USE_OMP
  AFNI_OMP_START ;
#pragma omp parallel
  {
    int ss,vv,rv,ii,kbest , ithr , qstep ;
    float *iv ; vector y ;  /* private arrays for each thread */
    double *ws ;
    FILE *mfp=NULL ;

    qstep = vstep / maxthr ;

#pragma omp critical (MALLOC)
 {
   iv = (float *)malloc(sizeof(float)*(niv+1)) ;
   vector_initialize(&y) ; vector_create_noinit(ntime,&y) ;
   ws = (double *)malloc(sizeof(double)*nws) ;
   if( virtu_mrv ){
     mfp = fopen(fname_mrv,"r") ;
     if( mfp == NULL ) ERROR_exit("cannot re-open temp file %s",fname_mrv) ;
   }
 }
   ithr = omp_get_thread_num() ;

#pragma omp for
     for( rv=0 ; rv < nmask ; rv++ ){
       vv = vvar[rv] ;
       if( ithr == 0 && qstep && rv%qstep==1 ) vstep_print() ;
#pragma omp critical (MEMCPY)
 {
       if( inset_mrv != NULL ){
         if( virtu_mrv ) THD_vector_fromfile( inset_mrv->nvals , rv , iv , mfp ) ;
         else            VECTIM_extract( inset_mrv , rv , iv ) ;
       } else {
         (void)THD_extract_array( vv , inset , 0 , iv ) ;  /* data vector */
       }
 }
       for( ii=0 ; ii < ntime ; ii++ ) y.elts[ii] = (double)iv[goodlist[ii]] ;
       ss = vv / nsliper ;  /* slice index in Xsli and RCsli */
       if( RCsli[ss] == NULL )
         ERROR_exit("NULL slice setup inside OpenMP loop!!!") ; /* really bad */
       kbest = REML_find_best_case( &y , RCsli[ss] , nws,ws ) ;  /* the work */
       aar[vv] = RCsli[ss]->rs[kbest]->rho ;
       bar[vv] = RCsli[ss]->rs[kbest]->barm ;
       rar[vv] = ws[0] ;

#ifdef REML_DEBUG
#pragma omp critical (FPP)
     { if( fpp != NULL ){
         int qq ;
         fprintf(fpp,"%d ",vv) ;
         fprintf(fpp," y=") ;
         for( qq=0 ; qq < ntime ; qq++ ) fprintf(fpp," %g",y.elts[qq]) ;
         fprintf(fpp,"  R=") ;
         for( qq=1 ; qq <= nab ; qq++ ) fprintf(fpp," %g",ws[qq]) ;
         fprintf(fpp,"\n") ;
       } }
#endif

     } /* end of REML loop over voxels */

#pragma omp critical (MALLOC)
   { free(ws); free(iv); vector_destroy(&y); if(mfp!=NULL) fclose(mfp); } /* destroy private copies */

  } /* end OpenMP */
  AFNI_OMP_END ;
#else
  ERROR_exit("This code should never be executed!!!") ; /* really bad */
#endif
    free(vvar) ;
#ifdef REML_DEBUG
    if( fpp != NULL ) fclose(fpp) ;
#endif
  }

     if( vstep ) fprintf(stderr,"\n") ;
     if( usetemp_rcol )               /* purge final slice's setup to disk? */
       reml_collection_save( RCsli[nsli-1] ) ;

     /*----- median filter (a,b)? -----*/

     if( do_mfilt ){
       MRI_IMAGE *afilt , *bfilt ;
       if( verb > 1 )
         ININFO_message("Median filtering best fit ARMA parameters") ;
       afilt = mri_medianfilter( aim , mfilt_radius , mask , 0 ) ;
       bfilt = mri_medianfilter( bim , mfilt_radius , mask , 0 ) ;
       if( afilt == NULL || bfilt == NULL ){
         WARNING_message("Median filter failed?! This is weird.") ;
         mri_free(afilt) ; mri_free(bfilt) ;
       } else {
         mri_free(aim) ; aim = afilt ; aar = MRI_FLOAT_PTR(aim) ;
         mri_free(bim) ; bim = bfilt ; bar = MRI_FLOAT_PTR(bim) ;
       }
     }

     if( verb )
       ININFO_message(
         "ARMA voxel parameters estimated: total CPU=%.2f Elapsed=%.2f",
         COX_cpu_time(),COX_clock_time() ) ;
     MEMORY_CHECK ;

   } /***** end of REML estimation *****/

   /*------- at this point, aim and bim contain the (a,b) parameters -------*/
   /*------- (either from -ABfile or from REML loop just done above) -------*/

   /*-- set up indexing and labels needed for bucket dataset creation --*/

   if( (do_buckt || do_eglt) && glt_num > 0 ){
     int ibot ;
STATUS("setting up glt_ind") ;
     glt_ind = (GLT_index **)calloc( sizeof(GLT_index *) , glt_num ) ;

     if( do_glt ){
       glt_ind[0] = create_GLT_index( 0, glt_mat[0]->rows,
                                      0, 0, 1 , do_rstat , glt_lab[0] ) ;
       kk = glt_ind[0]->ivtop + 1 ; ibot = 1 ;
     } else {
       kk = 0 ; ibot = 0 ;
     }
     for( ii=ibot ; ii < glt_num ; ii++ ){
STATUS(" creating glt_ind") ;
       glt_ind[ii] = create_GLT_index( kk, glt_mat[ii]->rows ,
                                       1 , do_tstat ,
                                           do_fstat , do_rstat , glt_lab[ii] ) ;
       if( glt_ind[ii] == NULL ) ERROR_exit("Cannot create GLT_index[%d]!?",ii) ; /* really bad */
       kk = glt_ind[ii]->ivtop + 1 ;
     }
     nbuckt = glt_ind[glt_num-1]->ivtop + 1 ;  /* number of sub-bricks */
     if( do_eglt ){
       if( do_glt )
         neglt = glt_ind[glt_num-1]->ivtop - glt_ind[oglt_num-1]->ivtop ;
       else
         neglt = glt_ind[glt_num-1]->ivtop + 1 ;
     }
   }

   /*----- now, use these values to compute the Generalized Least  -----*/
   /*----- Squares (GLSQ) solution at each voxel, and save results -----*/

   doing_nods = 0 ;  /* first time through, don't do nods */

GLSQ_LOOPBACK_dsort_nods:  /* for the -nods option [27 Jul 2015] */

   Rbeta_dset = create_float_dataset( inset , nbetaset , Rbeta_prefix,1 , &Rbeta_fn,&Rbeta_fp ) ;
   if( Rbeta_dset != NULL && beta_lab != NULL ){
STATUS("labelizing Rbeta") ;
     for( ii=0 ; ii < nbetaset ; ii++ )
       EDIT_BRICK_LABEL( Rbeta_dset , ii , beta_lab[betaset[ii]] ) ;
   }

   Rvar_dset  = create_float_dataset( inset , 6    , Rvar_prefix,1 , NULL,NULL ) ;
   if( Rvar_dset != NULL ){
     float abar[3] ;
STATUS("labelizing Rvar") ;
     EDIT_BRICK_LABEL( Rvar_dset , 0 , "a" ) ;
     EDIT_BRICK_LABEL( Rvar_dset , 1 , "b" ) ;
     EDIT_BRICK_LABEL( Rvar_dset , 2 , "lam" ) ;
     EDIT_BRICK_LABEL( Rvar_dset , 3 , "StDev" ) ;
     EDIT_BRICK_LABEL( Rvar_dset , 4 , "-LogLik") ;

     EDIT_BRICK_LABEL( Rvar_dset , 5 , "LjungBox") ;  /* 21 Jan 2020 */
     if( LJ_hh == 0 ){              /* set the max lag parameter now */
       int h1 = min_run/8 , h2 = (int)rintf(3.0f*logf((float)min_run)) ;
       LJ_hh = nrega+2+MIN(h1,h2) ; if( LJ_hh > min_run/2 ) LJ_hh = min_run/2 ;
       INFO_message("Ljung-Box max lag parameter h = %d (%d chi-squared DOF)",LJ_hh,LJ_hh-2) ;
     }
     EDIT_BRICK_TO_FICT( Rvar_dset , 5 , (LJ_hh-2.0f) ) ;

     abar[0] = rhomax ; abar[1] = bmax ; abar[2] = (float)nlevab ;
     THD_set_float_atr( Rvar_dset->dblk , "REMLFIT_abmax" , 3 , abar ) ;
   }

   Rfitts_dset = create_float_dataset( inset , nfull, Rfitts_prefix,0 , &Rfitts_fn,&Rfitts_fp ) ;
   Rerrts_dset = create_float_dataset( inset , nfull, Rerrts_prefix,0 , &Rerrts_fn,&Rerrts_fp ) ;
   Rwherr_dset = create_float_dataset( inset , nfull, Rwherr_prefix,0 , &Rwherr_fn,&Rwherr_fp ) ;

   Rbuckt_dset = create_float_dataset( inset , nbuckt,Rbuckt_prefix,1 , &Rbuckt_fn,&Rbuckt_fp ) ;
   if( Rbuckt_dset != NULL ){
     int nr ;
STATUS("setting up Rbuckt") ;
     for( ii=0 ; ii < glt_num ; ii++ ){
       if( glt_ind[ii] == NULL ) continue ;
       nr = glt_ind[ii]->nrow ;
       if( glt_ind[ii]->beta_ind != NULL ){
         for( jj=0 ; jj < nr ; jj++ ){
           EDIT_BRICK_LABEL( Rbuckt_dset , glt_ind[ii]->beta_ind[jj] ,
                                           glt_ind[ii]->beta_lab[jj]  ) ;
           EDIT_BRICK_TO_NOSTAT( Rbuckt_dset , glt_ind[ii]->beta_ind[jj] ) ;
         }
       }
       if( glt_ind[ii]->ttst_ind != NULL ){
         for( jj=0 ; jj < nr ; jj++ ){
           EDIT_BRICK_LABEL( Rbuckt_dset , glt_ind[ii]->ttst_ind[jj] ,
                                           glt_ind[ii]->ttst_lab[jj]  ) ;
           EDIT_BRICK_TO_FITT( Rbuckt_dset , glt_ind[ii]->ttst_ind[jj] , ddof ) ;
         }
       }
       if( glt_ind[ii]->ftst_ind >= 0 ){
         EDIT_BRICK_LABEL( Rbuckt_dset , glt_ind[ii]->ftst_ind ,
                                         glt_ind[ii]->ftst_lab  ) ;
         EDIT_BRICK_TO_FIFT( Rbuckt_dset , glt_ind[ii]->ftst_ind , nr , ddof ) ;
       }
       if( glt_ind[ii]->rtst_ind >= 0 ){
         EDIT_BRICK_LABEL( Rbuckt_dset , glt_ind[ii]->rtst_ind ,
                                         glt_ind[ii]->rtst_lab  ) ;
         EDIT_BRICK_TO_FIBT( Rbuckt_dset , glt_ind[ii]->rtst_ind, 0.5*nr,0.5*ddof );
       }
     }
   }

   Rglt_dset = create_float_dataset( inset , neglt , Rglt_prefix,1 , &Rglt_fn,&Rglt_fp ) ;
   if( Rglt_dset != NULL ){
     int nr , isub = glt_ind[oglt_num]->ivbot ;
STATUS("setting up Rglt") ;
     for( ii=oglt_num ; ii < glt_num ; ii++ ){
       if( glt_ind[ii] == NULL ) continue ;  /* should not happen! */
       nr = glt_ind[ii]->nrow ;
       if( glt_ind[ii]->beta_ind != NULL ){
         for( jj=0 ; jj < nr ; jj++ ){
           EDIT_BRICK_LABEL( Rglt_dset , glt_ind[ii]->beta_ind[jj]-isub ,
                                           glt_ind[ii]->beta_lab[jj]  ) ;
           EDIT_BRICK_TO_NOSTAT( Rglt_dset , glt_ind[ii]->beta_ind[jj]-isub ) ;
         }
       }
       if( glt_ind[ii]->ttst_ind != NULL ){
         for( jj=0 ; jj < nr ; jj++ ){
           EDIT_BRICK_LABEL( Rglt_dset , glt_ind[ii]->ttst_ind[jj]-isub ,
                                           glt_ind[ii]->ttst_lab[jj]  ) ;
           EDIT_BRICK_TO_FITT( Rglt_dset , glt_ind[ii]->ttst_ind[jj]-isub , ddof ) ;
         }
       }
       if( glt_ind[ii]->ftst_ind >= 0 ){
         EDIT_BRICK_LABEL( Rglt_dset , glt_ind[ii]->ftst_ind-isub ,
                                         glt_ind[ii]->ftst_lab  ) ;
         EDIT_BRICK_TO_FIFT( Rglt_dset , glt_ind[ii]->ftst_ind-isub , nr , ddof ) ;
       }
       if( glt_ind[ii]->rtst_ind >= 0 ){
         EDIT_BRICK_LABEL( Rglt_dset , glt_ind[ii]->rtst_ind-isub ,
                                         glt_ind[ii]->rtst_lab  ) ;
         EDIT_BRICK_TO_FIBT( Rglt_dset , glt_ind[ii]->rtst_ind-isub, 0.5*nr,0.5*ddof );
       }
     }
   }

   do_Rstuff = (Rbeta_dset  != NULL) || (Rvar_dset   != NULL) ||
               (Rfitts_dset != NULL) || (Rbuckt_dset != NULL) ||
               (Rerrts_dset != NULL) || (Rwherr_dset != NULL) ||
               (Rglt_dset   != NULL)                             ;

   /*---------------------------------------------------------------------*/
   /*---------------- and do the second (GLSQ) voxel loop ----------------*/

   /* reload the vectim? */

   if( virtu_mrv ){
     INFO_message("reloading data vectors from temp file") ;
     THD_vectim_reload_fromfile( inset_mrv , fname_mrv ) ;
     remove(fname_mrv) ;
     ININFO_message(" - and temp file %s has been deleted",fname_mrv) ;
     free(fname_mrv) ; fname_mrv = NULL ; virtu_mrv = 0 ;
   }

   /* setup for -dsort data extractificationizing */

   nconst_dsort = 0 ;
   if( num_dsort > 0 && dsortar_mean == NULL ){
     int dd , vv , nzz ; float *dvv ;

     /* create vectims for the dsorts? */

     if( inset_mrv != NULL ){
       dsortar_mrv = (MRI_vectim **)calloc(sizeof(MRI_vectim *),num_dsort) ;
       for( dd=0 ; dd < num_dsort ; dd++ ){
         DSET_load(dsortar->ar[dd]) ;
         dsortar_mrv[dd] = THD_dset_to_vectim( dsortar->ar[dd] , mask , 0 ) ;
         if( dsortar_mrv[dd] != NULL ) DSET_unload(dsortar->ar[dd]) ;
         else                          ERROR_message("Cannot create vector image from -dsort!?") ;

         if( dsortar_mrv != NULL )
           THD_check_vectim(dsortar_mrv[dd],"3dREMLfit -dsort") ;
       }
     }

     /* make some workspaces for -dsort */

     dsort_Zmat = (matrix *)malloc(sizeof(matrix)) ;
     matrix_initialize(dsort_Zmat) ;
     matrix_create    (ntime,num_dsort,dsort_Zmat) ;
     dsort_iv = (float *)malloc(sizeof(float)*(niv+1)) ;
     dsort_jv = (float *)malloc(sizeof(float)*(niv+1)) ;

     /* compute mean time series (in mask) from each dsort dataset */

     dsortar_mean = (float **)malloc(sizeof(float *)*num_dsort) ;
     for( dd=0 ; dd < num_dsort ; dd++ ){
       dsortar_mean[dd] = (float *)calloc(nvals,sizeof(float)) ;
       if( dsortar_mrv != NULL && dsortar_mrv[dd] != NULL ){
         for( vv=0 ; vv < dsortar_mrv[dd]->nvec ; vv++ ){
           dvv = VECTIM_PTR(dsortar_mrv[dd],vv) ;
           for( ii=0 ; ii < ntime ; ii++ ) dsortar_mean[dd][ii] += dvv[ii] ;
         }
       } else {
         MRI_IMAGE *dim ;
         for( ii=0 ; ii < ntime ; ii++ ){
           dim = THD_extract_float_brick(ii,dsortar->ar[dd]) ;
           dvv = MRI_FLOAT_PTR(dim) ;
           for( vv=0 ; vv < DSET_NVOX(dsortar->ar[dd]) ; vv++ ){
             if( INMASK(vv) ) dsortar_mean[dd][ii] += dvv[vv] ;
           }
         }
       }
       for( nzz=ii=0 ; ii < ntime ; ii++ ){
         dsortar_mean[dd][ii] /= nmask ;
         if( dsortar_mean[dd][ii] == 0.0f ) nzz++ ;
       }
       if( nzz == ntime ){ /* this should not happen, I hope */
         WARNING_message("-dsort #%d mean timeseries is all zero -- randomizing :-(",dd) ;
         for( ii=0 ; ii < ntime ;ii++ ) dsortar_mean[dd][ii] = drand48()-0.5 ;
       }
     }
   }  /* end of setting up -dsort stuff */

   if( do_Rstuff ){
     double *bbar[7] , bbsumq ;  /* workspace for REML_func() [24 Jun 2009] */
     double *bb1 , *bb2 , *bb3 , *bb4 , *bb5 , *bb6 , *bb7 ;
     vector qq5 ;

     /* how many regressors will be used this time through? */

     if( num_dsort > 0 && !doing_nods ) nregu = nregda ; /* 27 Jul 2015 */
     else                               nregu = nrega  ;

     last_nods_trip = (num_dsort == 0) || (num_dsort > 0 && doing_nods) ;

     /* make some space for vectors */
     for( ii=0 ; ii < 7 ; ii++ )
       bbar[ii] = (double *)malloc(sizeof(double)*(2*ntime+66)) ;
     bb1 = bbar[0] ; bb2 = bbar[1] ; bb3 = bbar[2] ;
     bb4 = bbar[3] ; bb5 = bbar[4] ; bb6 = bbar[5] ; bb7 = bbar[6] ;
     vector_initialize(&qq5) ; vector_create_noinit(nregu,&qq5) ;

     /* ss = slice index, rv = VECTIM index, vv = voxel index */

     if( vstep )             { fprintf(stderr,"++ GLSQ loop:") ; vn = 0 ; }
     if( vstep && dsort_nods ) fprintf(stderr,"{%s}",(doing_nods)?"nods":"dsort") ;
     for( ss=-1,rv=vv=0 ; vv < nvox ; vv++ ){
       if( vstep && vv%vstep==vstep-1 ) vstep_print() ;
       if( !INMASK(vv) ) continue ;

       /* extract data vector */

       if( inset_mrv != NULL ){ /* 05 Nov 2008 */
         VECTIM_extract( inset_mrv , rv , iv ) ; rv++ ;
       } else {
         (void)THD_extract_array( vv , inset , 0 , iv ) ;  /* data vector */
       }
       AAmemcpy( jv , iv , sizeof(float)*nfull ) ;         /* copy of data */
       for( ii=0 ; ii < ntime ; ii++ ) y.elts[ii] = iv[goodlist[ii]] ;

       /* extract dsort vectors into dsort_Zmat matrix? [22 Jul 2015] */

       if( num_dsort > 0 && !doing_nods ){
         int dd ; int nzz ; double zsum ;
         for( dd=0 ; dd < num_dsort ; dd++ ){
           if( dsortar_mrv != NULL && dsortar_mrv[dd] != NULL ){
             VECTIM_extract( dsortar_mrv[dd] , rv-1 , dsort_iv ) ;
           } else {
             (void)THD_extract_array( vv , dsortar->ar[dd] , 0 , dsort_iv ) ;
           }
           for( zsum=0.0,ii=0 ; ii < ntime ; ii++ ){
             dsort_Zmat->elts[ii][dd] = dsort_iv[goodlist[ii]] ;
             zsum += dsort_Zmat->elts[ii][dd] ;
           }
           zsum /= ntime ;
           if( dmbase ){
             for( nzz=ii=0 ; ii < ntime ;ii++ ){
               dsort_Zmat->elts[ii][dd] -= zsum ;
               if( dsort_Zmat->elts[ii][dd] == 0.0 ) nzz++ ;
             }
           } else {
             for( nzz=ii=0 ; ii < ntime ; ii++ )
               if( dsort_Zmat->elts[ii][dd] == zsum ) nzz++ ;
           }
           if( nzz == ntime ){
#if 0
             WARNING_message("at voxel %d (%d,%d,%d), dsort #%d is constant == %g -- replacing :-(",
                             vv, DSET_index_to_ix(dsortar->ar[dd],vv) ,
                                 DSET_index_to_jy(dsortar->ar[dd],vv) ,
                                 DSET_index_to_kz(dsortar->ar[dd],vv) , zsum , dd ) ;
#endif
             nconst_dsort++ ;
             for( ii=0 ; ii < ntime ;ii++ ) dsort_Zmat->elts[ii][dd] = dsortar_mean[dd][ii] ;
           }
         }
       }

       ssold = ss ; ss = vv / nsliper ; /* ss = slice index in Xsli and RCsli */

       /*=== If at a new slice:
               remove REML setups (except a=b=0 case) for previous slice;
               create new slice matrices, if they don't exist already, OR
               get new slice matrices back from disk, if they were purged
               earlier; then add GLT setups to the new slice.
             Note that the code assumes that the slice index ss is
               non-decreasing as the voxel index increases.
             The purpose of doing it this way is to save memory space.  ======*/

       if( ss > ssold ){                                   /* at a new slice! */
         STATUS("new slice") ;
         if( ssold >= 0 && last_nods_trip )
           reml_collection_destroy( RCsli[ssold] , 1 ) ;
         if( RCsli[ss] == NULL ){              /* create this slice setup now */
           if( verb > 1 && vstep ) fprintf(stderr,"+") ;
           RCsli[ss] = REML_setup_all( Xsli[ss] , tau , nlevab, rhomax,bmax ) ;
           if( RCsli[ss] == NULL ) ERROR_exit("REML setup fails for ss=%d",ss) ; /* really bad */
         } else if( RC_SAVED(RCsli[ss]) ){               /* restore from disk */
           if( verb > 1 && vstep ) fprintf(stderr,"+") ;
           reml_collection_restore( RCsli[ss] ) ;
         }
         for( kk=0 ; kk < glt_num ; kk++ )     /* add GLT stuff to this setup */
           REML_add_glt_to_all( RCsli[ss] , glt_mat[kk] ) ;
       }

       if( abfixed )  /* special case */
         jj = 1 ;
       else
         jj = IAB(RCsli[ss],aar[vv],bar[vv]) ; /* closest point in (a,b) grid */

       /* select/create the REML setup to use [22 Jul 2015] */

       my_rset = NULL ; my_Xmat = NULL ; my_Xsmat = NULL ; my_kill = 0 ; rsetp_dsort = NULL ;
       if( num_dsort > 0 && !doing_nods ){    /* create modified REML setup for */
         int   ia  = jj % (1+RCsli[ss]->na);  /* voxel-specific regression matrix */
         int   ib  = jj / (1+RCsli[ss]->na);  /* the na+1 denom is correct here - RWC */
         double aaa = RCsli[ss]->abot + ia * RCsli[ss]->da;
         double bbb = RCsli[ss]->bbot + ib * RCsli[ss]->db;

         /* glue dsort_Zmat to X, then do the REML setup via REML_setup_one */
         /* (this voxel-wise REML setup is why -dsort is so slow) */

         rsetp_dsort = REML_setup_plus( RCsli[ss]->X , dsort_Zmat , tau , aaa,bbb ) ;
         if( rsetp_dsort == NULL ){  /* should not happen */
           ERROR_message("cannot compute REML_setup_plus() at voxel %d",vv) ;
         }
         my_rset  = rsetp_dsort->rset ;
         my_Xmat  = rsetp_dsort->X ;
         my_Xsmat = rsetp_dsort->Xs ;
         my_kill  = 1 ;  /* flag to kill my_stuff when done with it */
         for( kk=0 ; kk < glt_num ; kk++ )     /* include GLTs */
           REML_add_glt_to_one( my_rset , glt_mat[kk] ) ;
       }

       if( my_rset == NULL && RCsli[ss]->rs[jj] == NULL ){ /* try to fix up this oversight */
         int   ia  = jj % (1+RCsli[ss]->na);               /* by creating needed setup now */
         int   ib  = jj / (1+RCsli[ss]->na);
         double aaa = RCsli[ss]->abot + ia * RCsli[ss]->da;
         double bbb = RCsli[ss]->bbot + ib * RCsli[ss]->db;

         STATUS("new setup?") ;
         RCsli[ss]->rs[jj] = REML_setup_one( Xsli[ss] , tau , aaa,bbb ) ;
         for( kk=0 ; kk < glt_num ; kk++ )     /* make sure GLTs are included */
           REML_add_glt_to_one( RCsli[ss]->rs[jj] , glt_mat[kk] ) ;
       }
       if( my_rset == NULL ){
         my_rset  = RCsli[ss]->rs[jj] ;
         my_Xmat  = RCsli[ss]->X ;
         my_Xsmat = RCsli[ss]->Xs ;
       }
       if( my_rset == NULL ){ /* should never happen */
         ERROR_message("bad REML! voxel #%d (%d,%d,%d) has a=%.3f b=%.3f lam=%.3f jj=%d",
                         vv, DSET_index_to_ix(inset,vv) ,
                             DSET_index_to_jy(inset,vv) ,
                             DSET_index_to_kz(inset,vv) ,
                         aar[vv],bar[vv],LAMBDA(aar[vv],bar[vv]),jj) ;
       } else {

         /*--- do the fitting; various results are in the bb? vectors:
                bb5 = estimated betas       (nregu)
                bb6 = fitted model          (ntime)
                bb7 = whitened fitted model (ntime) [not used below]
                bb1 = whitened data         (ntime) [not used below]
                bb2 = whitened residuals    (ntime)
                      (sum of squares of bb2 = bbsumq = noise variance) ------*/

         sprintf(sslab,"%s %d", "fitting" ,vv); STATUS(sslab) ; /* debugging */
#if 1
         (void)REML_func( &y , my_rset , my_Xmat , my_Xsmat , bbar , &bbsumq ) ;
#else
         (void)REML_func( &y , RCsli[ss]->rs[jj] , RCsli[ss]->X , RCsli[ss]->Xs ,
                          bbar , &bbsumq ) ;
#endif

         /*--------- scatter the results to various datasets ---------*/

         if( Rfitts_dset != NULL ){  /* note that iv still contains original data */
           for( ii=0 ; ii < ntime ; ii++ ) iv[goodlist[ii]] = bb6[ii] ;
           sprintf(sslab,"%s %d", "Rfitts" ,vv); /* debugging */
           save_series( vv , Rfitts_dset , nfull , iv , Rfitts_fp ) ;
         }

         if( Rerrts_dset != NULL ){  /* jv contains copy of original data */
           if( ntime < nfull ) for( ii=0 ; ii < nfull ; ii++ ) iv[ii] = 0.0f ;
           for( ii=0 ; ii < ntime ; ii++ )
             iv[goodlist[ii]] = jv[goodlist[ii]] - bb6[ii] ;
           sprintf(sslab,"%s %d", "Rerrts" ,vv); /* debugging */
           save_series( vv , Rerrts_dset , nfull , iv , Rerrts_fp ) ;
         }

         if( Rwherr_dset != NULL ){  /* note there is no Owherr dataset! */
           if( ntime < nfull ) for( ii=0 ; ii < nfull ; ii++ ) iv[ii] = 0.0f ;
           for( ii=0 ; ii < ntime ; ii++ )
             iv[goodlist[ii]] = bb2[ii] ;
           sprintf(sslab,"%s %d", "Rwherr" ,vv); /* debugging */
           save_series( vv , Rwherr_dset , nfull , iv , Rwherr_fp ) ;
         }

         if( Rbeta_dset != NULL ){
           for( ii=0 ; ii < nbetaset ; ii++ ) iv[ii] = bb5[betaset[ii]] ;
           sprintf(sslab,"%s %d", "Rbeta" ,vv); /* debugging */
           save_series( vv , Rbeta_dset , nbetaset , iv , Rbeta_fp ) ;
         }

         if( Rvar_dset != NULL ){
           int h1,h2 ;

           iv[0] = my_rset->rho ; iv[1] = my_rset->barm ;
           iv[2] = my_rset->lam ; iv[3] = sqrt( bbsumq / ddof ) ;
           iv[4] = (rar != NULL) ? rar[vv] : 0.0f ;

           /* Ljung-Box statistic (cf. thd_ljungbox.c) [21 Jan 2020] */
           iv[5] = (float)ljung_box_uneven( ntime, LJ_hh, bb2, tau ) ;

           sprintf(sslab,"%s %d", "Rvar" ,vv); /* debugging */
           save_series( vv , Rvar_dset , 6 , iv , Rvar_fp ) ;
         }

         AAmemcpy( qq5.elts , bb5 , sizeof(double)*nregu ) ; /* 24 Jun 2009 */

         if( glt_num > 0 && Rbuckt_dset != NULL ){
           double gv ; GLT_index *gin ; int nr ;
           memset( iv , 0 , sizeof(float)*niv ) ;
           for( kk=0 ; kk < glt_num ; kk++ ){
             gin = glt_ind[kk] ; if( gin == NULL ) continue ; /* skip this'n */
             nr = gin->nrow ;
             { sprintf(sslab,"%s %d %d", "Rbuckt glt" ,vv,kk); STATUS(sslab) ; }
             gv = REML_compute_gltstat( ddof , &y , &qq5 , bbsumq  ,
                                        my_rset , my_rset->glt[kk] ,
                                        glt_mat[kk] , glt_smat[kk] ,
                                        my_Xmat , my_Xsmat          ) ;
             if( gin->ftst_ind >= 0 ) iv[gin->ftst_ind] = gv ;
             if( gin->rtst_ind >= 0 ) iv[gin->rtst_ind] = betaR ;
             if( gin->beta_ind != NULL && betaG->dim >= nr ){
               for( ii=0 ; ii < nr ; ii++ ){
                 iv[gin->beta_ind[ii]] = betaG->elts[ii] ;
               }
             }
             if( gin->ttst_ind != NULL && betaT->dim >= nr ){
               for( ii=0 ; ii < nr ; ii++ ){
                 iv[gin->ttst_ind[ii]] = betaT->elts[ii] ;
               }
             }
           }
           sprintf(sslab,"%s %d", "Rbuckt glt" ,vv); /* debugging */
           save_series( vv , Rbuckt_dset , nbuckt , iv , Rbuckt_fp ) ;
         }

         if( Rglt_dset != NULL ){
           double gv ; GLT_index *gin ; int nr , isub ;
           memset( iv , 0 , sizeof(float)*niv ) ;
           isub = glt_ind[oglt_num]->ivbot ;  /* first index in first extra GLT */
           for( kk=oglt_num ; kk < glt_num ; kk++ ){
             gin = glt_ind[kk] ; if( gin == NULL ) continue ; /* skip this'n */
             nr = gin->nrow ;
             gv = REML_compute_gltstat( ddof , &y , &qq5 , bbsumq  ,
                                        my_rset , my_rset->glt[kk] ,
                                        glt_mat[kk] , glt_smat[kk] ,
                                        my_Xmat , my_Xsmat          ) ;
             if( gin->ftst_ind >= 0 ) iv[gin->ftst_ind-isub] = gv ;
             if( gin->rtst_ind >= 0 ) iv[gin->rtst_ind-isub] = betaR ;
             if( gin->beta_ind != NULL && betaG->dim >= nr ){
               for( ii=0 ; ii < nr ; ii++ ){
                 iv[gin->beta_ind[ii]-isub] = betaG->elts[ii] ;
               }
             }
             if( gin->ttst_ind != NULL && betaT->dim >= nr ){
               for( ii=0 ; ii < nr ; ii++ ){
                 iv[gin->ttst_ind[ii]-isub] = betaT->elts[ii] ;
               }
             }
           }
           sprintf(sslab,"%s %d", "Rglt" ,vv); /* debugging */
           save_series( vv , Rglt_dset , neglt , iv , Rglt_fp ) ;
         }

         if( my_kill ){                   /* 22 Jun 2015 */
           reml_setup_plus_destroy(rsetp_dsort) ;
           my_rset = NULL ; my_Xmat = NULL ; my_Xsmat = NULL ; my_kill = 0 ; rsetp_dsort = NULL ;
         }

       } /* end of if actual have a reml_setup to process this data with */

     } /* end of voxel loop */

     if( last_nods_trip )
       reml_collection_destroy( RCsli[nsli-1] , 1 ) ;  /* keep just izero case (for OLSQ) */

     for( ii=0 ; ii < 7 ; ii++ ) free(bbar[ii]) ;
     vector_destroy(&qq5) ;

     if( vstep ) fprintf(stderr,"\n") ;
     if( verb )
       ININFO_message("GLSQ regression done: total CPU=%.2f Elapsed=%.2f",
                      COX_cpu_time(),COX_clock_time() ) ;
     if( nconst_dsort > 0 )
       WARNING_message("%d voxels had constant -dsort vectors replaced by -dsort mean vector",nconst_dsort) ;
     MEMORY_CHECK ;
   } /* end of if(do_Rstuff) */

   /* macro to add FDR curves to a dataset and print a message */

#undef  FDRIZE
#define FDRIZE(ds)                                                            \
 do{ if( do_FDR && !AFNI_noenv("AFNI_AUTOMATIC_FDR") ){                       \
       ii = THD_count_fdrwork(ds) ;                                           \
       if( verb > 1 && ii*nmask > 666666 )                                    \
         INFO_message("creating FDR curves in dataset %s",DSET_FILECODE(ds)); \
       ii = THD_create_all_fdrcurves(ds) ;                                    \
     } else {                                                                 \
       ii = 0 ;                                                               \
     }                                                                        \
     if( ii > 0 && verb > 1 )                                                 \
       ININFO_message("Added %d FDR curve%s to dataset %s",                   \
                      ii , (ii==1)?"":"s" , DSET_FILECODE(ds) ) ;             \
 } while(0)

   /*----------------- write output REML datasets to disk -----------------*/

   if( Rbeta_dset != NULL ){
     populate_dataset( Rbeta_dset , mask , Rbeta_fn,Rbeta_fp ) ;
     DSET_write(Rbeta_dset); WROTE_DSET(Rbeta_dset); DSET_deletepp(Rbeta_dset);
     MEMORY_CHECK ;
   }
   if( Rvar_dset  != NULL ){
     populate_dataset( Rvar_dset , mask , Rvar_fn,Rvar_fp ) ;
     DSET_write(Rvar_dset); WROTE_DSET(Rvar_dset); DSET_deletepp(Rvar_dset);
     MEMORY_CHECK ;
   }
   if( Rfitts_dset != NULL ){
     populate_dataset( Rfitts_dset , mask , Rfitts_fn,Rfitts_fp ) ;
     DSET_write(Rfitts_dset); WROTE_DSET(Rfitts_dset); DSET_deletepp(Rfitts_dset);
     MEMORY_CHECK ;
   }
   if( Rerrts_dset != NULL ){
     populate_dataset( Rerrts_dset , mask , Rerrts_fn,Rerrts_fp ) ;
     DSET_write(Rerrts_dset); WROTE_DSET(Rerrts_dset); DSET_deletepp(Rerrts_dset);
     MEMORY_CHECK ;
   }
   if( Rwherr_dset != NULL ){
     populate_dataset( Rwherr_dset , mask , Rwherr_fn,Rwherr_fp ) ;
     DSET_write(Rwherr_dset); WROTE_DSET(Rwherr_dset); DSET_deletepp(Rwherr_dset);
     MEMORY_CHECK ;
   }
   if( Rbuckt_dset != NULL ){
     populate_dataset( Rbuckt_dset , mask , Rbuckt_fn,Rbuckt_fp ) ;
     FDRIZE(Rbuckt_dset) ;
     DSET_write(Rbuckt_dset); WROTE_DSET(Rbuckt_dset); DSET_deletepp(Rbuckt_dset);
     MEMORY_CHECK ;
   }
   if( Rglt_dset != NULL ){
     populate_dataset( Rglt_dset , mask , Rglt_fn,Rglt_fp ) ;
     FDRIZE(Rglt_dset) ;
     DSET_write(Rglt_dset); WROTE_DSET(Rglt_dset); DSET_deletepp(Rglt_dset);
     MEMORY_CHECK ;
   }

   /** If we just did -dsort case, and we are supposed to do -dsort_nods
       case, then alter the prefixes and do all the above again [27 Jul 2015] **/

#undef  PREFIX_NODSIZE
#define PREFIX_NODSIZE(ppp)                                         \
 do{ char *qp = modify_afni_prefix((ppp),NULL,dsort_nods_suffix) ;  \
     if( qp != NULL ){ free((ppp)) ; (ppp) = qp ; }                 \
 } while(0)

   if( do_Rstuff && num_dsort > 0 && dsort_nods && !doing_nods ){
     PREFIX_NODSIZE(Rbeta_prefix ) ;
     PREFIX_NODSIZE(Rvar_prefix  ) ;
     PREFIX_NODSIZE(Rbuckt_prefix) ;
     PREFIX_NODSIZE(Rfitts_prefix) ;
     PREFIX_NODSIZE(Rerrts_prefix) ;
     PREFIX_NODSIZE(Rwherr_prefix) ;
     PREFIX_NODSIZE(Rglt_prefix  ) ;

     doing_nods = 1 ; goto GLSQ_LOOPBACK_dsort_nods ;
   }

   /*-------------------------------------------------------------------------*/
   /*---------------------- create OLSQ outputs, if any ----------------------*/

   doing_nods = 0 ;  /* first time through, don't do nods */

OLSQ_LOOPBACK_dsort_nods:  /* for the -nods option [27 Jul 2015] */

   Obeta_dset = create_float_dataset( inset , nbetaset , Obeta_prefix,1 , &Obeta_fn,&Obeta_fp ) ;
   if( Obeta_dset != NULL && beta_lab != NULL ){
     for( ii=0 ; ii < nbetaset ; ii++ )
       EDIT_BRICK_LABEL( Obeta_dset , ii , beta_lab[betaset[ii]] ) ;
   }

   Ovar_dset  = create_float_dataset( inset , 1 , Ovar_prefix,1  , NULL,NULL ) ;
   if( Ovar_dset != NULL ){
     EDIT_BRICK_LABEL( Ovar_dset , 0 , "StDev" ) ;
   }

   Ofitts_dset = create_float_dataset( inset , nfull, Ofitts_prefix,0 , &Ofitts_fn,&Ofitts_fp ) ;
   Oerrts_dset = create_float_dataset( inset , nfull, Oerrts_prefix,0 , &Oerrts_fn,&Oerrts_fp ) ;

   Obuckt_dset = create_float_dataset( inset , nbuckt,Obuckt_prefix,1 , &Obuckt_fn,&Obuckt_fp ) ;
   if( Obuckt_dset != NULL ){
     int nr ;
     for( ii=0 ; ii < glt_num ; ii++ ){
       if( glt_ind[ii] == NULL ) continue ;
       nr = glt_ind[ii]->nrow ;
       if( glt_ind[ii]->beta_ind != NULL ){
         for( jj=0 ; jj < nr ; jj++ ){
           EDIT_BRICK_LABEL( Obuckt_dset , glt_ind[ii]->beta_ind[jj] ,
                                           glt_ind[ii]->beta_lab[jj]  ) ;
           EDIT_BRICK_TO_NOSTAT( Obuckt_dset , glt_ind[ii]->beta_ind[jj] ) ;
         }
       }
       if( glt_ind[ii]->ttst_ind != NULL ){
         for( jj=0 ; jj < nr ; jj++ ){
           EDIT_BRICK_LABEL( Obuckt_dset , glt_ind[ii]->ttst_ind[jj] ,
                                           glt_ind[ii]->ttst_lab[jj]  ) ;
           EDIT_BRICK_TO_FITT( Obuckt_dset , glt_ind[ii]->ttst_ind[jj] , ddof ) ;
         }
       }
       if( glt_ind[ii]->ftst_ind >= 0 ){
         EDIT_BRICK_LABEL( Obuckt_dset , glt_ind[ii]->ftst_ind ,
                                         glt_ind[ii]->ftst_lab  ) ;
         EDIT_BRICK_TO_FIFT( Obuckt_dset , glt_ind[ii]->ftst_ind , nr , ddof ) ;
       }
       if( glt_ind[ii]->rtst_ind >= 0 ){
         EDIT_BRICK_LABEL( Obuckt_dset , glt_ind[ii]->rtst_ind ,
                                         glt_ind[ii]->rtst_lab  ) ;
         EDIT_BRICK_TO_FIBT( Obuckt_dset , glt_ind[ii]->rtst_ind, 0.5*nr,0.5*ddof );
       }
     }
   }

   Oglt_dset = create_float_dataset( inset , neglt , Oglt_prefix,1 , &Oglt_fn,&Oglt_fp ) ;
   if( Oglt_dset != NULL ){
     int nr , isub = glt_ind[oglt_num]->ivbot ;
     for( ii=oglt_num ; ii < glt_num ; ii++ ){
       if( glt_ind[ii] == NULL ) continue ;  /* should not happen! */
       nr = glt_ind[ii]->nrow ;
       if( glt_ind[ii]->beta_ind != NULL ){
         for( jj=0 ; jj < nr ; jj++ ){
           EDIT_BRICK_LABEL( Oglt_dset , glt_ind[ii]->beta_ind[jj]-isub ,
                                           glt_ind[ii]->beta_lab[jj]  ) ;
           EDIT_BRICK_TO_NOSTAT( Oglt_dset , glt_ind[ii]->beta_ind[jj]-isub ) ;
         }
       }
       if( glt_ind[ii]->ttst_ind != NULL ){
         for( jj=0 ; jj < nr ; jj++ ){
           EDIT_BRICK_LABEL( Oglt_dset , glt_ind[ii]->ttst_ind[jj]-isub ,
                                           glt_ind[ii]->ttst_lab[jj]  ) ;
           EDIT_BRICK_TO_FITT( Oglt_dset , glt_ind[ii]->ttst_ind[jj]-isub , ddof ) ;
         }
       }
       if( glt_ind[ii]->ftst_ind >= 0 ){
         EDIT_BRICK_LABEL( Oglt_dset , glt_ind[ii]->ftst_ind-isub ,
                                         glt_ind[ii]->ftst_lab  ) ;
         EDIT_BRICK_TO_FIFT( Oglt_dset , glt_ind[ii]->ftst_ind-isub , nr , ddof ) ;
       }
       if( glt_ind[ii]->rtst_ind >= 0 ){
         EDIT_BRICK_LABEL( Oglt_dset , glt_ind[ii]->rtst_ind-isub ,
                                         glt_ind[ii]->rtst_lab  ) ;
         EDIT_BRICK_TO_FIBT( Oglt_dset , glt_ind[ii]->rtst_ind-isub, 0.5*nr,0.5*ddof );
       }
     }
   }

   do_Ostuff = (Obeta_dset  != NULL) || (Ovar_dset   != NULL) ||
               (Ofitts_dset != NULL) || (Obuckt_dset != NULL) ||
               (Oerrts_dset != NULL) || (Oglt_dset   != NULL)   ;

   /*--------------------------------------------------------------------*/
   /*---------------- and do the third (OLSQ) voxel loop ----------------*/

   if( do_Ostuff ){
     double *bbar[7] , bbsumq ;  /* workspace for REML_func() [24 Jun 2009] */
     double *bb1 , *bb2 , *bb3 , *bb4 , *bb5 , *bb6 , *bb7 ;
     vector qq5 ;

     /* how many regressors will be used this time through? */

     if( num_dsort > 0 && !doing_nods ) nregu = nregda ; /* 27 Jul 2015 */
     else                               nregu = nrega  ;

     last_nods_trip = (num_dsort == 0) || (num_dsort > 0 && doing_nods) ;

     for( ii=0 ; ii < 7 ; ii++ )
       bbar[ii] = (double *)malloc(sizeof(double)*(2*ntime+66)) ;
     bb1 = bbar[0] ; bb2 = bbar[1] ; bb3 = bbar[2] ;
     bb4 = bbar[3] ; bb5 = bbar[4] ; bb6 = bbar[5] ; bb7 = bbar[6] ;
     vector_initialize(&qq5) ; vector_create_noinit(nregu,&qq5) ;

     /* rv = VECTIM index, vv = voxel index */

     nconst_dsort = 0 ;
     if( vstep )             { fprintf(stderr,"++ OLSQ loop:") ; vn = 0 ; }
     if( vstep && dsort_nods ) fprintf(stderr,"{%s}",(doing_nods)?"nods":"dsort") ;
     for( rv=vv=0 ; vv < nvox ; vv++ ){
       if( vstep && vv%vstep==vstep-1 ) vstep_print() ;
       if( !INMASK(vv) ) continue ;

       /* extract data vector */

       if( inset_mrv != NULL ){ /* 05 Nov 2008 */
         VECTIM_extract( inset_mrv , rv , iv ) ; rv++ ;
       } else {
         (void)THD_extract_array( vv , inset , 0 , iv ) ;  /* data vector */
       }
       AAmemcpy( jv , iv , sizeof(float)*nfull ) ;
       for( ii=0 ; ii < ntime ; ii++ ) y.elts[ii] = (double)iv[goodlist[ii]] ;

       /* extract dsort vectors into dsort_Zmat matrix? [22 Jul 2015] */

       if( num_dsort > 0 && !doing_nods ){
         int dd ; int nzz ; double zsum ;
         for( dd=0 ; dd < num_dsort ; dd++ ){
           if( dsortar_mrv != NULL && dsortar_mrv[dd] != NULL ){
             VECTIM_extract( dsortar_mrv[dd] , rv-1 , dsort_iv ) ;
           } else {
             (void)THD_extract_array( vv , dsortar->ar[dd] , 0 , dsort_iv ) ;
           }
           for( zsum=0.0,ii=0 ; ii < ntime ; ii++ ){
             dsort_Zmat->elts[ii][dd] = dsort_iv[goodlist[ii]] ;
             zsum += dsort_Zmat->elts[ii][dd] ;
           }
           zsum /= ntime ;
           if( dmbase ){
             for( nzz=ii=0 ; ii < ntime ;ii++ ){
               dsort_Zmat->elts[ii][dd] -= zsum ;
               if( dsort_Zmat->elts[ii][dd] == 0.0 ) nzz++ ;
             }
           } else {
             for( nzz=ii=0 ; ii < ntime ; ii++ )
               if( dsort_Zmat->elts[ii][dd] == zsum ) nzz++ ;
           }
           if( nzz == ntime ){
#if 0
             WARNING_message("at voxel %d (%d,%d,%d), dsort #%d is constant == %g -- replacing :-(",
                              vv, DSET_index_to_ix(dsortar->ar[dd],vv) ,
                                  DSET_index_to_jy(dsortar->ar[dd],vv) ,
                                  DSET_index_to_kz(dsortar->ar[dd],vv) , zsum , dd ) ;
#endif
             nconst_dsort++ ;
             for( ii=0 ; ii < ntime ;ii++ ) dsort_Zmat->elts[ii][dd] = dsortar_mean[dd][ii] ;
           }
         }
       }

       jj = izero ;                       /* OLSQ (a,b)=(0,0) case */
       ss = vv / nsliper ;                /* slice index */

       /* select/create the REML setup to use [22 Jul 2015] */

       my_rset = NULL ; my_Xmat = NULL ; my_Xsmat = NULL ; my_kill = 0 ; rsetp_dsort = NULL ;
       if( num_dsort > 0 && !doing_nods ){    /* create modified REML setup for */
         int   ia  = jj % (1+RCsli[ss]->na);  /* voxel-specific regression matrix */
         int   ib  = jj / (1+RCsli[ss]->na);
         double aaa = RCsli[ss]->abot + ia * RCsli[ss]->da;
         double bbb = RCsli[ss]->bbot + ib * RCsli[ss]->db;

         /* glue dsort_Zmat to X, then do the REML setup via REML_setup_one */
         rsetp_dsort = REML_setup_plus( RCsli[ss]->X , dsort_Zmat , tau , aaa,bbb ) ;
         if( rsetp_dsort == NULL ){  /* should not happen */
           ERROR_message("cannot compute REML_setup_plus() at voxel %d",vv) ;
         }
         my_rset  = rsetp_dsort->rset ;
         my_Xmat  = rsetp_dsort->X ;
         my_Xsmat = rsetp_dsort->Xs ;
         my_kill  = 1 ;  /* flag to kill my_stuff when done with it */
         for( kk=0 ; kk < glt_num ; kk++ )     /* include GLTs */
           REML_add_glt_to_one( my_rset , glt_mat[kk] ) ;
       }

       if( my_rset == NULL && RCsli[ss]->rs[jj] == NULL ){ /* try to fix up this oversight */
         int   ia  = jj % (1+RCsli[ss]->na);
         int   ib  = jj / (1+RCsli[ss]->na);
         double aaa = RCsli[ss]->abot + ia * RCsli[ss]->da;
         double bbb = RCsli[ss]->bbot + ib * RCsli[ss]->db;

         RCsli[ss]->rs[jj] = REML_setup_one( Xsli[ss] , tau , aaa,bbb ) ;
       }
       if( my_rset == NULL ){
         my_rset  = RCsli[ss]->rs[jj] ;
         my_Xmat  = RCsli[ss]->X ;
         my_Xsmat = RCsli[ss]->Xs ;
       }
       if( my_rset == NULL ){ /* should never happen */
         ERROR_message("bad OLSQ! voxel #%d (%d,%d,%d) jj=%d",
                         vv, DSET_index_to_ix(inset,vv) ,
                             DSET_index_to_jy(inset,vv) ,
                             DSET_index_to_kz(inset,vv) , jj ) ;
       } else {

         /* 17 May 2010: if Rstuff wasn't run, have to add GLTs now */

         if( my_rset->nglt == 0 ){
           for( kk=0 ; kk < glt_num ; kk++ )
             REML_add_glt_to_one( my_rset , glt_mat[kk]) ;
         }

#if 1
         (void)REML_func( &y , my_rset , my_Xmat , my_Xsmat , bbar , &bbsumq ) ;
#else

         (void)REML_func( &y , RCsli[ss]->rs[jj] , RCsli[ss]->X , RCsli[ss]->Xs ,
                          bbar , &bbsumq ) ;
#endif

         if( Ofitts_dset != NULL ){
           for( ii=0 ; ii < ntime ; ii++ ) iv[goodlist[ii]] = bb6[ii] ;
           sprintf(sslab,"%s %d", "Ofitts" ,vv); /* debugging */
           save_series( vv , Ofitts_dset , nfull , iv , Ofitts_fp ) ;
         }

         if( Oerrts_dset != NULL ){  /* jv contains copy of original data */
           if( ntime < nfull ) for( ii=0 ; ii < nfull ; ii++ ) iv[ii] = 0.0f ;
           for( ii=0 ; ii < ntime ; ii++ )
             iv[goodlist[ii]] = jv[goodlist[ii]] - bb6[ii] ;
           sprintf(sslab,"%s %d", "Oerrts" ,vv); /* debugging */
           save_series( vv , Oerrts_dset , nfull , iv , Oerrts_fp ) ;
         }

         if( Obeta_dset != NULL ){
           for( ii=0 ; ii < nbetaset ; ii++ ) iv[ii] = bb5[betaset[ii]] ;
           sprintf(sslab,"%s %d", "Obeta" ,vv); /* debugging */
           save_series( vv , Obeta_dset , nbetaset , iv , Obeta_fp ) ;
         }

         if( Ovar_dset != NULL ){
           iv[0] = sqrt( bbsumq / ddof ) ;
           sprintf(sslab,"%s %d", "Ovar" ,vv); /* debugging */
           save_series( vv , Ovar_dset , 1 , iv , Ovar_fp ) ;
         }

         AAmemcpy( qq5.elts , bb5 , sizeof(double)*nregu ) ; /* 24 Jun 2009 */

         if( glt_num > 0 && Obuckt_dset != NULL ){
           double gv ; GLT_index *gin ; int nr ;
           memset( iv , 0 , sizeof(float)*niv ) ;
           for( kk=0 ; kk < glt_num ; kk++ ){
             gin = glt_ind[kk] ; if( gin == NULL ) continue ; /* skip this'n */
             nr = gin->nrow ;
             gv = REML_compute_gltstat( ddof , &y , &qq5 , bbsumq  ,
                                        my_rset , my_rset->glt[kk] ,
                                        glt_mat[kk] , glt_smat[kk] ,
                                        my_Xmat , my_Xsmat          ) ;
             if( gin->ftst_ind >= 0 ) iv[gin->ftst_ind] = gv ;
             if( gin->rtst_ind >= 0 ) iv[gin->rtst_ind] = betaR ;
             if( gin->beta_ind != NULL && betaG->dim >= nr ){
               for( ii=0 ; ii < nr ; ii++ ){
                 iv[gin->beta_ind[ii]] = betaG->elts[ii] ;
               }
             }
             if( gin->ttst_ind != NULL && betaT->dim >= nr ){
               for( ii=0 ; ii < nr ; ii++ ){
                 iv[gin->ttst_ind[ii]] = betaT->elts[ii] ;
               }
             }
           }
           sprintf(sslab,"%s %d", "Obuckt glt" ,vv); /* debugging */
           save_series( vv , Obuckt_dset , nbuckt , iv , Obuckt_fp ) ;
         }

         if( Oglt_dset != NULL ){
           double gv ; GLT_index *gin ; int nr , isub ;
           memset( iv , 0 , sizeof(float)*niv ) ;
           isub = glt_ind[oglt_num]->ivbot ;  /* first index in first extra GLT */
           for( kk=oglt_num ; kk < glt_num ; kk++ ){
             gin = glt_ind[kk] ; if( gin == NULL ) continue ; /* skip this'n */
             nr = gin->nrow ;
             gv = REML_compute_gltstat( ddof , &y , &qq5 , bbsumq  ,
                                        my_rset , my_rset->glt[kk] ,
                                        glt_mat[kk] , glt_smat[kk] ,
                                        my_Xmat , my_Xsmat          ) ;
             if( gin->ftst_ind >= 0 ) iv[gin->ftst_ind-isub] = gv ;
             if( gin->rtst_ind >= 0 ) iv[gin->rtst_ind-isub] = betaR ;
             if( gin->beta_ind != NULL && betaG->dim >= nr ){
               for( ii=0 ; ii < nr ; ii++ ){
                 iv[gin->beta_ind[ii]-isub] = betaG->elts[ii] ;
               }
             }
             if( gin->ttst_ind != NULL && betaT->dim >= nr ){
               for( ii=0 ; ii < nr ; ii++ ){
                 iv[gin->ttst_ind[ii]-isub] = betaT->elts[ii] ;
               }
             }
           }
           sprintf(sslab,"%s %d", "Oglt" ,vv); /* debugging */
           save_series( vv , Oglt_dset , neglt , iv , Oglt_fp ) ;
         }

         if( my_kill ){                   /* 22 Jun 2015 */
           reml_setup_plus_destroy(rsetp_dsort) ;
           my_rset = NULL ; my_Xmat = NULL ; my_Xsmat = NULL ; my_kill = 0 ; rsetp_dsort = NULL ;
         }

       }
     } /* end of voxel loop */

     for( ii=0 ; ii < 7 ; ii++ ) free(bbar[ii]) ;
     vector_destroy(&qq5) ;

     if( vstep ) fprintf(stderr,"\n") ;
     if( verb > 1 )
       ININFO_message("OLSQ regression done: total CPU=%.2f Elapsed=%.2f",
                      COX_cpu_time(),COX_clock_time() ) ;
     if( nconst_dsort > 0 )
       WARNING_message("%d voxels had constant -dsort vectors replaced",nconst_dsort) ;
   }

   /*----------------- done with the input dataset -----------------*/

   /* delete lots of memory that's no longer needed */

   MEMORY_CHECK ;

   if( last_nods_trip ){
     if( verb > 1 ) ININFO_message("unloading input dataset and REML matrices");
STATUS("inset_mrv") ;
     if( inset_mrv != NULL ) VECTIM_destroy(inset_mrv) ; /* 05 Nov 2008 */
STATUS("inset") ;
     DSET_delete(inset) ; free(jv) ; free(iv) ; free(tau) ;
     if( num_dsort > 0 ){                                /* 22 Jun 2015 */
       int dd ;
STATUS("dsortar") ;
       for( dd=0 ; dd < num_dsort ; dd++ ) DSET_delete(dsortar->ar[dd]) ;
       if( dsortar_mrv != NULL ){
STATUS("dsortar_mrv") ;
         for( dd=0 ; dd < num_dsort ; dd++ )
           if( dsortar_mrv[dd] != NULL ) VECTIM_destroy(dsortar_mrv[dd]) ;
       }
       if( dsortar_mean != NULL ){
STATUS("dsortar_mean") ;
         for( dd=0 ; dd < num_dsort ; dd++ )
           if( dsortar_mean[dd] != NULL ) free(dsortar_mean[dd] ) ;
       }
     }
STATUS("RCsli") ;
     for( ss=0 ; ss < nsli ; ss++ ){
       reml_collection_destroy(RCsli[ss],0) ; matrix_destroy(Xsli[ss]) ;
     }
     free(RCsli) ; free(Xsli) ;
          if( abset != NULL ) DSET_delete(abset) ;
     else if( aim   != NULL ) { mri_free(aim) ; mri_free(bim) ; }
STATUS("ALL_GLTS") ;
     KILL_ALL_GLTS ;
     MEMORY_CHECK ;
   }

   /*-------------- write output OLSQ datasets to disk --------------*/

   if( Obeta_dset != NULL ){
     populate_dataset( Obeta_dset , mask , Obeta_fn,Obeta_fp ) ;
     DSET_write(Obeta_dset); WROTE_DSET(Obeta_dset); DSET_deletepp(Obeta_dset);
     MEMORY_CHECK ;
   }
   if( Ovar_dset  != NULL ){
     populate_dataset( Ovar_dset , mask , Ovar_fn,Ovar_fp ) ;
     DSET_write(Ovar_dset); WROTE_DSET(Ovar_dset); DSET_deletepp(Ovar_dset);
     MEMORY_CHECK ;
   }
   if( Ofitts_dset != NULL ){
     populate_dataset( Ofitts_dset , mask , Ofitts_fn,Ofitts_fp ) ;
     DSET_write(Ofitts_dset); WROTE_DSET(Ofitts_dset); DSET_deletepp(Ofitts_dset);
     MEMORY_CHECK ;
   }
   if( Oerrts_dset != NULL ){
     populate_dataset( Oerrts_dset , mask , Oerrts_fn,Oerrts_fp ) ;
     DSET_write(Oerrts_dset); WROTE_DSET(Oerrts_dset); DSET_deletepp(Oerrts_dset);
     MEMORY_CHECK ;
   }
   if( Obuckt_dset != NULL ){
     populate_dataset( Obuckt_dset , mask , Obuckt_fn,Obuckt_fp ) ;
     FDRIZE(Obuckt_dset) ;
     DSET_write(Obuckt_dset); WROTE_DSET(Obuckt_dset); DSET_deletepp(Obuckt_dset);
     MEMORY_CHECK ;
   }
   if( Oglt_dset != NULL ){
     populate_dataset( Oglt_dset , mask , Oglt_fn,Oglt_fp ) ;
     FDRIZE(Oglt_dset) ;
     DSET_write(Oglt_dset); WROTE_DSET(Oglt_dset); DSET_deletepp(Oglt_dset);
     MEMORY_CHECK ;
   }

   /** rinse and repeat? [27 Jul 2015] */

   if( do_Ostuff && num_dsort > 0 && dsort_nods && !doing_nods ){
     PREFIX_NODSIZE(Oerrts_prefix) ;
     PREFIX_NODSIZE(Oglt_prefix  ) ;
     PREFIX_NODSIZE(Obeta_prefix ) ;
     PREFIX_NODSIZE(Ovar_prefix  ) ;
     PREFIX_NODSIZE(Obuckt_prefix) ;
     PREFIX_NODSIZE(Ofitts_prefix) ;

     doing_nods = 1 ; goto OLSQ_LOOPBACK_dsort_nods ;
   }

   /*----------------------------- Free at last ----------------------------*/

   INFO_message("3dREMLfit is all done! total CPU=%.2f Elapsed=%.2f",
                COX_cpu_time() , COX_clock_time() ) ;
   if( gmask != NULL && gmask != mask ) free(gmask) ;  /* 27 Mar 2009 */
   free(mask) ; MEMORY_CHECK ;
#if 0
#ifdef USING_MCW_MALLOC
   if( verb > 4 ) mcw_malloc_dump() ;
#endif
#endif
   exit(0) ;
}

/******************************************************************************/
/******************************************************************************/
#if 0
/*--- The shell script below is for testing this ARMA/REML implementation. ---*/
/*-----------------------------------------------------------------------------

#!/bin/tcsh

### Script to test REML GLSQ vs OLSQ regression

# B      = signal amplitude for all repetitions
# P      = signal period (TRs)
# nstim  = number of signals (IM regression)
# numvox = number of voxels to simulate
# so there is a total of $nstim * $numvox stimuli being simulated

set B      = 2
set P      = 12
set nstim  = 20
set numvox = 400

# ARMA(1,1) parameters for this test/simulation

set AA  = 0.8
set LAM = 0.5

# D = number of time points (TR=1)

@ D = $P * $nstim

# create stimulus timing

1deval -num $nstim -expr "i*${P}"  > stim.1D

# create the voxel time series = simulated data

1deval -num $D -expr "${B}*sin(PI*t/${P})^2"  > signal.1D
foreach ii ( `count -dig 4 1 $numvox` )
  1dgenARMA11 -num $D -a $AA -lam $LAM               > noise.1D
  1deval      -a noise.1D -b signal.1D -expr 'a+b'   > data${ii}.1D
end

# glue them together into one file

1dcat data0*.1D > data.1D
\rm -f data0*.1D noise.1D signal.1D

# create the regression matrix (note use of IM regression model)

3dDeconvolve -num_stimts 1                                            \
             -stim_times_IM 1 stim.1D "EXPR(0,${P}) sin(PI*t/${P})^2" \
             -stim_label    1 'sinsq'                                 \
             -nodata $D 1 -x1D_stop -polort 2 -x1D test.xmat.1D

# analyses

3dREMLfit -matrix test.xmat.1D \
          -input data.1D\'     \
          -Rvar  test.Rvar.1D  \
          -Rbeta test.Rbeta.1D \
          -Obeta test.Obeta.1D \
          -nobout -Grid 5 -MAXa 0.9 -MAXb 0.9 -NEGcor

# extract the betas for each voxel into one long single column 1D file
# instead of the multi-column file output by 3dREMLfit

@ ns1 = $nstim - 1
if( -f test.Rbeta.all.1D ) \rm test.Rbeta.all.1D
if( -f test.Obeta.all.1D ) \rm test.Obeta.all.1D
foreach ii ( `count -dig 1 0 $ns1` )
  1dcat test.Rbeta.1D"[$ii]" >> test.Rbeta.all.1D
  1dcat test.Obeta.1D"[$ii]" >> test.Obeta.all.1D
end

# compute the mean and stdev of the GLSQ and OLSQ betas
# (means should be about B, or something weird happened)

3dTstat -mean -stdev -prefix test.Rbeta.stat.1D test.Rbeta.all.1D\'
3dTstat -mean -stdev -prefix test.Obeta.stat.1D test.Obeta.all.1D\'

# compute the ratio of the stdevs
# srat > 1 means OLSQ stdev was bigger than GLSQ (what we expect)

set Rsig = `1dcat test.Rbeta.stat.1D'[1]'`
set Osig = `1dcat test.Obeta.stat.1D'[1]'`
set srat = `ccalc "$Osig/$Rsig"`

# print out these results

echo "======================="
echo "a = $AA  lam = $LAM"
echo "REML mean stdev = " `1dcat test.Rbeta.stat.1D`
echo "OLSQ mean stdev = " `1dcat test.Obeta.stat.1D`
echo "Osig/Rsig       =  $srat"
echo "======================="

time ; exit 0

-----------------------------------------------------------------------------*/
#endif
/******************************************************************************/
/******************************************************************************/
