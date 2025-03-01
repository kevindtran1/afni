#include "SUMA_suma.h"

void usage_SurfLocalStat (SUMA_GENERIC_ARGV_PARSE *ps)
{
      static char FuncName[]={"usage_SurfLocalStat"};
      char      * s = NULL;
      int         i;

      printf ( "\n"
      "SurfLocalStat - compute local statistics on a surface\n\n"
      "     Local statistics are those computed over the neighborhood of\n"
      "     each node, possibly restricted to a mask.\n"
      "\n"
      "     Neighborhoods and distances are defined on a trangulated surface\n"
      "     mesh.  They will differ between smoothwm and pial, for example.\n"
      "     The neighborhood of a given node is defined by nodes within a\n"
      "     specified distance of the given node (along the surface).\n"
      "\n"
      "     For each node's neighborhood, statistics are computed from data\n"
      "     values associated with those nodes, such as MRI intensities,\n"
      "     beta weights or ROI index values.\n"
      "\n"
      "usage:.\n"
      "\n"
      " -hood R     = Neighborhood of node n consists of nodes within R \n"
      " -nbhd_rad R = distance from n as measured by the shortest \n"
      "               distance along the mesh.\n"
      "               (-hood and -nbhd_rad are equivalent)\n"
      " -prefix PREFIX = Prefix of output data set.\n"
      " -stat sss   = Compute the statistic named 'sss' on the values\n"
      "               extracted from the region around each voxel:\n"
      "               * mean   = average of the values\n"
      "               * mode   = most common value\n"
/* **** uncomment these as they become ready ****
      "               * stdev  = standard deviation\n" 
      "               * var    = variance (stdev*stdev)\n"
      "               * cvar   = coefficient of variation = stdev/fabs(mean)\n"
      "               * median = median of the values\n"
      "               * MAD    = median absolute deviation\n"
      "               * min    = minimum\n"
      "               * max    = maximum\n"
      "               * absmax = maximum of the absolute values\n"
** **** */
      "               * num    = number of the values in the region:\n"
      "                          with the use of -mask or -automask,\n"
      "                          the size of the region around any given\n"
      "                          voxel will vary; this option lets you\n"
      "                          map that size.  It may be useful if you\n"
      "                          plan to compute a t-statistic (say) from\n"
      "                          the mean and stdev outputs.\n"
      "               * FWHM   = compute (like 3dFWHM) image smoothness\n"
      "                          inside each voxel's neighborhood.  Results\n"
      "                          are in 3 sub-bricks: FWHMx, FHWMy, and FWHM.\n"
      "                          Places where an output is -1 are locations\n"
      "                          where the FWHM value could not be computed\n"
      "                          (e.g., outside the mask).\n"
      "               * ALL    = all of the above, in that order\n"
      "               More than one '-stat' option can be used.\n"
      "\n"
      "\n");

      printf("------------------------------------------------------------\n");
      printf("examples:\n\n");
      
      printf(
      "1. count the number of nodes in each node's local neighborhood\n"
      "   (the -input data will not matter in this case)\n"
      "\n"
      "      SurfLocalstat -hood 5 -stat num                         \\\n"
      "                    -i_gii std.141.lh.smoothwm.gii            \\\n"
      "                    -input std.141.lh.thickness.niml.dset     \\\n"
      "                    -prefix std.141.lh.local_nnode.niml.dset\n"
      "\n"
      "2. smooth locally, output the mean over each neighbornood\n"
      "\n"
      "      SurfLocalstat -hood 5 -stat mean                        \\\n"
      "                    -i_gii std.141.lh.smoothwm.gii            \\\n"
      "                    -input std.141.lh.thickness.niml.dset     \\\n"
      "                    -prefix std.141.lh.local_mean_5.niml.dset\n"
      "\n"
      "3. perform modal smoothing on a FreeSurfer parcellation dataset\n"
      "   - smooth in small neighborhoods of 'radius' 2mm\n"
      "   - use 3dRank to first convert to a more usable form (can improve)\n"
      "   - include suma commands to compare input vs output\n"
      "\n"
      "      3dRank -prefix std.141.lh.aparc.a2009s_RANK.niml.dset \\\n"
      "             -input std.141.lh.aparc.a2009s.annot.niml.dset\n"
      "\n"
      "      SurfLocalstat -hood 2 -stat mode                       \\\n"
      "           -i_gii std.141.lh.smoothwm.gii                    \\\n"
      "           -input std.141.lh.aparc.a2009s_RANK.niml.dset     \\\n"
      "           -prefix std.141.lh.aparc.RANK_smooth_2.niml.dset\n"
      "\n"
      "      suma -spec std.141.FT_lh.spec -sv FT_SurfVol.nii       \\\n"
      "           -input std.141.lh.aparc.a2009s_RANK.niml.dset &\n"
      "\n"
      "      suma -spec std.141.FT_lh.spec -sv FT_SurfVol.nii       \\\n"
      "           -input std.141.lh.aparc.RANK_smooth_2.niml.dset &\n"
      "\n");

      s = SUMA_help_basics();
      printf("------------------------------------------------------------\n");
      printf("general and global options:\n%s", s);
      SUMA_free(s);

      s  = SUMA_help_IO_Args(ps);
      printf("------------------------------------------------------------\n");
      printf("surface input/output options:\n%s", s);
      SUMA_free(s);

      printf("       Ziad S. Saad SSCC/NIMH/NIH saadz@mail.nih.gov     \n");
      exit(0);
}

static int ncode=-1 , code[MAX_NCODE];


SUMA_GENERIC_PROG_OPTIONS_STRUCT *SUMA_SurfLocalStat_ParseInput(char *argv[], int argc, SUMA_GENERIC_ARGV_PARSE *ps)
{
   static char FuncName[]={"SUMA_SurfLocalStat_ParseInput"}; 
   SUMA_GENERIC_PROG_OPTIONS_STRUCT *Opt=NULL;
   int kar;
   SUMA_Boolean brk;
   SUMA_Boolean LocalHead = NOPE;

   SUMA_ENTRY;
   
   Opt = SUMA_Alloc_Generic_Prog_Options_Struct();
   Opt->ps = ps; /* for convenience */
   Opt->NodeDbg = -1;
   Opt->out_prefix = NULL;
   Opt->r = -1.0;
   ncode = 0;
   kar = 1;
   brk = NOPE;
   while (kar < argc) { /* loop across command line options */
      /*fprintf(stdout, "%s verbose: Parsing command line...\n", FuncName);*/
      if (strcmp(argv[kar], "-h") == 0 || strcmp(argv[kar], "-help") == 0) {
          usage_SurfLocalStat(ps);
          exit (0);
      }
      
      SUMA_SKIP_COMMON_OPTIONS(brk, kar);
      
      if (!brk && (strcmp(argv[kar], "-debug") == 0))
      {
         if (kar+1 >= argc)
         {
            fprintf (SUMA_STDERR, "need a number after -debug \n");
            exit (1);
         }
         
         Opt->debug = atoi(argv[++kar]);
         brk = YUP;
      }
      
      if (!brk && (strcmp(argv[kar], "-node_debug") == 0))
      {
         if (kar+1 >= argc)
         {
            fprintf (SUMA_STDERR, "need a node index after -node_debug \n");
            exit (1);
         }
         
         Opt->NodeDbg = atoi(argv[++kar]);
         brk = YUP;
      }
      
      if (!brk && (strcmp(argv[kar], "-prefix") == 0))
      {
         if (kar+1 >= argc)
         {
            fprintf (SUMA_STDERR, "need a dset prefix after -prefix \n");
            exit (1);
         }
         
         Opt->out_prefix = SUMA_copy_string(argv[++kar]);
         brk = YUP;
      }
      
      if (!brk && (strcmp(argv[kar], "-stat") == 0))
      {
         char *cpt ;
         if (kar+1 >= argc)
         {
            fprintf (SUMA_STDERR, "need an argument after -stat \n");
            exit (1);
         }
         ++kar;
         cpt = argv[kar] ; if( *cpt == '-' ) cpt++ ;
              if( strcasecmp(cpt,"mean")  == 0 ) code[ncode++] = NSTAT_MEAN  ;
         else if( strcasecmp(cpt,"stdev") == 0 ) code[ncode++] = NSTAT_SIGMA ;
         else if( strcasecmp(cpt,"var")   == 0 ) code[ncode++] = NSTAT_VAR   ;
         else if( strcasecmp(cpt,"cvar")  == 0 ) code[ncode++] = NSTAT_CVAR  ;
         else if( strcasecmp(cpt,"median")== 0 ) code[ncode++] = NSTAT_MEDIAN;
         else if( strcasecmp(cpt,"mode")  == 0 ) code[ncode++] = NSTAT_MODE  ;
         else if( strcasecmp(cpt,"MAD")   == 0 ) code[ncode++] = NSTAT_MAD   ;
         else if( strcasecmp(cpt,"min")   == 0 ) code[ncode++] = NSTAT_MIN   ;
         else if( strcasecmp(cpt,"max")   == 0 ) code[ncode++] = NSTAT_MAX   ;
         else if( strcasecmp(cpt,"absmax")== 0 ) code[ncode++] = NSTAT_ABSMAX;
         else if( strcasecmp(cpt,"num")   == 0 ) code[ncode++] = NSTAT_NUM   ;
         else if( strcasecmp(cpt,"fwhm")  == 0 ) code[ncode++] = NSTAT_FWHMx ;
         else if( strcasecmp(cpt,"ALL")   == 0 ){
            code[ncode++] = NSTAT_MEAN  ; code[ncode++] = NSTAT_SIGMA ;
            code[ncode++] = NSTAT_VAR   ; code[ncode++] = NSTAT_CVAR  ;
            code[ncode++] = NSTAT_MEDIAN; code[ncode++] = NSTAT_MODE  ;
            code[ncode++] = NSTAT_MAD   ;
            code[ncode++] = NSTAT_MIN   ; code[ncode++] = NSTAT_MAX   ;
            code[ncode++] = NSTAT_ABSMAX; code[ncode++] = NSTAT_NUM   ;
            code[ncode++] = NSTAT_FWHMx ; 
         }
         else {
            fprintf (SUMA_STDERR, 
               "-stat '%s' is an unknown statistic type",argv[kar]) ;
         }
         brk = YUP;
      }
      
      if (!brk && (strcmp(argv[kar], "-hood") == 0 || 
                   strcmp(argv[kar], "-nbhd_rad") == 0))
      {
         if (kar+1 >= argc)
         {
            fprintf (SUMA_STDERR, "need a value after -nbhd_rad \n");
            exit (1);
         }
         
         Opt->r = atof(argv[++kar]);
         if (Opt->r <= 0.0) {
            SUMA_S_Errv("neighborhood radius is not valid (have %f from %s).\n", 
                        Opt->r, argv[kar]);
            exit (1);
         }
         brk = YUP;
      }
      
      if (!brk && !ps->arg_checked[kar]) {
         SUMA_S_Errv("Option %s not understood. Try -help for usage\n", 
                     argv[kar]);
         exit (1);
      } else { 
         brk = NOPE;
         kar ++;
      }
   }
   
   if (!Opt->out_prefix) {
      Opt->out_prefix = SUMA_copy_string("SurfLocalstat");
   }
   if (Opt->r <= 0.0) {
      SUMA_S_Errv("neighborhood radius is not set (have %f).\n", Opt->r);
      exit (1);
   }
   if (!ncode) {
      SUMA_S_Note("No stat specified, doing -stat mean\n");
      code[0] = NSTAT_MEAN;
      ncode = 1;
   }
   SUMA_RETURN(Opt);
}

int main (int argc,char *argv[])
{/* Main */    
   static char FuncName[]={"SurfLocalstat"}; 
   SUMA_GENERIC_PROG_OPTIONS_STRUCT *Opt;  
   SUMA_GENERIC_ARGV_PARSE *ps=NULL;
   SUMA_DSET_FORMAT iform = SUMA_NO_DSET_FORMAT;
   SUMA_DSET *din=NULL, *dout=NULL;
   SUMA_SurfSpecFile *Spec = NULL;
   int i, N_Spec, N_inmask = -1;
   SUMA_SurfaceObject *SO=NULL, *SOf=NULL;
   char *ooo=NULL;
   SUMA_Boolean LocalHead = NOPE;
   
   SUMA_STANDALONE_INIT;
   SUMA_mainENTRY;

   /* Allocate space for DO structure */
   SUMAg_DOv = SUMA_Alloc_DisplayObject_Struct (SUMA_MAX_DISPLAYABLE_OBJECTS);
   ps = SUMA_Parse_IO_Args(argc, argv, "-i;-t;-spec;-m;-dset;-talk;");
   
   if (argc < 2) {
      usage_SurfLocalStat(ps);
      exit (1);
   }
   
   Opt = SUMA_SurfLocalStat_ParseInput (argv, argc, ps);

   if (Opt->debug > 2) LocalHead = YUP;
   if (Opt->ps->N_dsetname != 1) {
      SUMA_S_Errv("Need one and only one dset please."
                  "Have %d on command line.\n", Opt->ps->N_dsetname);
      exit(1);
   }
   if (!(din = SUMA_LoadDset_s (Opt->ps->dsetname[0], &iform, 0))) {
      SUMA_S_Errv("Failed to load dset named %s\n", Opt->ps->dsetname[0]);
      exit(1);
   }
   
   Spec = SUMA_IO_args_2_spec(ps, &N_Spec);
   if (N_Spec == 0) {
      SUMA_S_Err("No surfaces found.");
      exit(1);
   }
   if (N_Spec != 1) {
      SUMA_S_Err("Multiple spec at input.");
      exit(1);
   }

   SUMA_LH("Loading surface...");
   SO = SUMA_Load_Spec_Surf(Spec, 0, ps->sv[0], Opt->debug);
   if (!SO) {
         fprintf (SUMA_STDERR,"Error %s:\n"
                              "Failed to find surface\n"
                              "in spec file. \n",
                              FuncName );
         exit(1);
      
   }   
   if (Spec->N_Surfs == 2) { 
      SOf = SUMA_Load_Spec_Surf(Spec, 1, ps->sv[0], Opt->debug); 
      if (!SOf) {
         fprintf (SUMA_STDERR,"Error %s:\n"
                              "Failed to find surface\n"
                              "in spec file. \n",
                              FuncName );
         exit(1);
      }   
   } else { SOf = NULL; }
   
   if (!(Opt->nmask = SUMA_load_all_command_masks(Opt->ps->bmaskname, 
                              Opt->ps->nmaskname, Opt->ps->cmask, SO->N_Node, 
                              &N_inmask)) && N_inmask < 0) {
         SUMA_S_Err("Failed loading mask");
         exit(1);
   }

   if (!(dout = SUMA_CalculateLocalStats(SO, din, 
                                    Opt->nmask, 1,
                                    Opt->r, NULL,
                                    ncode, code, 
                                    NULL, Opt->NodeDbg,
                                    SOf))) {
      SUMA_S_Err("Failed in SUMA_CalculateLocalStats");
      exit(1);
   }
   
   /* write it out */
   ooo = SUMA_WriteDset_s(Opt->out_prefix, dout, iform, 
                           THD_ok_overwrite(), 0);
   SUMA_free(ooo); ooo=NULL;
   
   if (ps) SUMA_FreeGenericArgParse(ps); ps = NULL;
   if (Opt) Opt = SUMA_Free_Generic_Prog_Options_Struct(Opt);
   if (!SUMA_Free_CommonFields(SUMAg_CF)) 
      SUMA_error_message(FuncName,"SUMAg_CF Cleanup Failed!",1);
   
   exit(0);
   
} 
