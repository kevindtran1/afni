#!/bin/tcsh


@global_parse `basename $0` "$*" ; if ($status) exit 0

set version   = "0.1";  set rev_dat   = "Feb 8, 2021"
# [PT] start of this program in program form.  Programmatically speaking.
#
# ----------------------------------------------------------------

set this_prog = "adjunct_suma_fs_qc.tcsh"
set tpname    = "${this_prog:gas/@djunct_//}"
set here      = $PWD

set subj      = ""
set odir      = ""

set opref          = "IMG_${tpname}"

set pppp           = "`3dnewid -fun11`"
set wdir_name      = "__workdir_olap_${pppp}"
set tpref          = "for_img_qc"

set DO_CLEAN       = 1

# ------------------- process options, a la rr ----------------------

if ( $#argv == 0 ) goto SHOW_HELP

set ac = 1
while ( $ac <= $#argv )
    # terminal options
    if ( ("$argv[$ac]" == "-h" ) || ("$argv[$ac]" == "-help" )) then
        goto SHOW_HELP
    endif
    if ( "$argv[$ac]" == "-ver" ) then
        goto SHOW_VERSION
    endif

    #  ---------- inputs: required ---------------

    if ( "$argv[$ac]" == "-sid" ) then
        if ( $ac >= $#argv ) goto FAIL_MISSING_ARG
        @ ac += 1
        set subj = "$argv[$ac]"

    else if ( "$argv[$ac]" == "-suma_dir" ) then
        if ( $ac >= $#argv ) goto FAIL_MISSING_ARG
        @ ac += 1
        set odir = "$argv[$ac]"

    else if ( "$argv[$ac]" == "-no_clean" ) then
        set DO_CLEAN = 0

    else
        echo "\n\n** ERROR: unexpected option #$ac = '$argv[$ac]'\n\n"
        goto BAD_EXIT
        
    endif
    @ ac += 1
end

# =======================================================================
# ============================ ** SETUP ** ==============================
# =======================================================================

echo "++ Prepare for running ${this_prog} (ver = ${version})"

if ( "${subj}" == "" ) then
    echo "** ERROR: missing subj ID. Use '-sid ..'"
    goto BAD_EXIT
endif

if ( "${odir}" == "" ) then
    echo "** ERROR: missing SUMA/ directory location. Use '-suma_dir ..'"
    goto BAD_EXIT
endif

# ===================== output dir + wdir =======================
 
# check output directory, use input one if nothing given

\mkdir -p ${odir}

# place wdir in the odir
set wdir = "${odir}/${wdir_name}"

\mkdir -p ${wdir}

cd ${wdir}           # now all files we need are just "up"

# =========================== Actual Plots ==============================

set ulay      = ( ../${subj}*SurfVol.nii* )
set focus     = ( ../brainmask.nii* )
set dset_aa   = ( ../aparc+aseg_REN_all.nii* )

set ulay      = "${ulay[1]}"
set focus     = "${focus[1]}"
set dset_aa   = "${dset_aa[1]}"

set dset_tiss = dset_tiss.nii.gz
set dset_mask = dset_mask.nii.gz

# ------------ make intermediate dsets

# make the tissue seg dset
3dcalc                                                                    \
    -overwrite                                                            \
    -a ../aparc+aseg_REN_wmat.nii.gz                                      \
    -b ../aparc+aseg_REN_gm.nii.gz                                        \
    -c ../aparc+aseg_REN_vent.nii.gz                                      \
    -d ../aparc+aseg_REN_csf.nii.gz                                       \
    -e ../aparc+aseg_REN_othr.nii.gz                                      \
    -f ../aparc+aseg_REN_unkn.nii.gz                                      \
    -expr "step(a) + 2*step(b) + 3*step(c) + 4*(step(d)+step(e)+step(f))" \
    -prefix "${dset_tiss}"                                                \
    -datum short

if ( $status ) then
    echo "+* ERROR making tissue dset"
    goto BAD_EXIT
endif

3drefit -cmap INT_CMAP ${dset_tiss}

# make brain mask result(s)
3dcalc                                                                     \
    -a       "${focus}"                                                    \
    -b       "${dset_tiss}"                                                \
    -expr    "step(a)+step(b)"                                             \
    -prefix  "${dset_mask}"                                                \
    -datum   short

if ( $status ) then
    echo "+* ERROR making mask dset"
    goto BAD_EXIT
endif

# ---------------------------- make images ----------------------------------

# brain mask (and parcellation mask)
set opref = qc_00_fs_brainmask_${subj}

@chauffeur_afni                                                       \
    -ulay  ${ulay}                                                    \
    -olay  "${dset_mask}"                                             \
    -box_focus_slices ${focus}                                        \
    -ulay_range 0% 98%                                                \
    -func_range 3                                                     \
    -cbar_ncolors 2                                                   \
    -cbar_topval  ""                                                  \
    -cbar "2=red 1=red  .00066=none"                                  \
    -pbar_posonly                                                     \
    -thr_olay   1.0001                                                \
    -olay_alpha Yes                                                   \
    -olay_boxed Yes                                                   \
    -opacity     4                                                    \
    -blowup      2                                                    \
    -prefix      ${opref}                                             \
    -montx 6 -monty 1                                                 \
    -set_xhairs OFF                                                   \
    -label_mode 1 -label_size 3                                       \
    -do_clean

2dcat                                                             \
    -gap     5                                                    \
    -gap_col 150 150 150                                          \
    -nx 1                                                         \
    -ny 3                                                         \
    -prefix  ../${opref}.jpg                                      \
    ${opref}*{sag,axi,cor}*

# ----------------

# major tissue classifications
set opref = qc_01_fs_tiss_${subj}

@chauffeur_afni                                                       \
    -ulay  ${ulay}                                                    \
    -olay  ${dset_tiss}                                               \
    -box_focus_slices ${focus}                                        \
    -ulay_range 0% 110%                                               \
    -func_range 256                                                   \
    -cbar "ROI_glasbey_256 FLIP"                                      \
    -pbar_posonly                                                     \
    -opacity     6                                                    \
    -blowup      2                                                    \
    -prefix      ${opref}                                             \
    -montx 6 -monty 1                                                 \
    -set_xhairs OFF                                                   \
    -label_mode 1 -label_size 3                                       \
    -do_clean

2dcat                                                             \
    -gap     5                                                    \
    -gap_col 150 150 150                                          \
    -nx 1                                                         \
    -ny 3                                                         \
    -prefix  ../${opref}.jpg                                      \
    ${opref}*{sag,axi,cor}*

# ----------------

# aparc+aseg (2000) REN all map
set opref = qc_02_fs_aa_REN_all_${subj}

@chauffeur_afni                                                       \
    -ulay  ${ulay}                                                    \
    -olay  ${dset_aa}                                                 \
    -box_focus_slices ${focus}                                        \
    -ulay_range 0% 110%                                               \
    -func_range 256                                                   \
    -cbar ROI_i256                                                    \
    -pbar_posonly                                                     \
    -opacity     4                                                    \
    -blowup      2                                                    \
    -prefix      ${opref}                                             \
    -montx 6 -monty 1                                                 \
    -set_xhairs OFF                                                   \
    -label_mode 1 -label_size 3                                       \
    -do_clean

2dcat                                                             \
    -gap     5                                                    \
    -gap_col 150 150 150                                          \
    -nx 1                                                         \
    -ny 3                                                         \
    -prefix  ../${opref}.jpg                                      \
    ${opref}*{sag,axi,cor}*

# ------------------------------------------------

if ( ${DO_CLEAN} ) then
    echo "++ Cleanup"
    cd ..
    \rm -rf ${workdir_name}
endif

goto GOOD_EXIT

# ========================================================================
# ========================================================================

SHOW_HELP:
cat << EOF
-------------------------------------------------------------------------

OVERVIEW ~1~

In brief, this script is for quickly making some QC images for the
SUMA/ directory created by @SUMA_Make_Spec_FS after running
FreeSurfer's recon-all.  Phew, we made it.

written by PA Taylor.

# --------------------------------------------------------------------

USAGE ~1~

This program has the following options:

    -sid          SUBJ_ID    :(req) subject ID

    -suma_dir     SUMA_DIR   :(req) SUMA/ directory output by AFNI's
                              @SUMA_Make_Spec_FS

    -no_clean                :(opt) do not remove temporary working 
                              subdirectory (def: remove it)

    -help                    :(opt) show help

    -hview                   :(opt) show help in text editor

    -ver                     :(opt) show version

OUTPUT ~1~

This script makes three *.jpg files in the specified SUMA/
directory. The underlay in each is the *SurfVol.nii* dset.  Each JPG
is row of axial, sagittal and coronal montages around the volumes
defined by the brainmask.nii*:

    qc_00*.jpg  : the overlay is the brainmask.nii* volume in red, and
                  the subset of that volume that was parcellated by FS
                  (in either the "2000" or "2009" atlases) is outlined
                  in black.

                  The idea for this formatting is that we do want to
                  see the official FS brainmask, but we might also
                  want to note its differences with the the binarized
                  aparc+aseg file.  We might prefer using one or the
                  other dsets as a mask for other work.

    qc_01*.jpg  : the overlay is a set of tissues, like a segmentation
                  map of 4 classes: GM, WM, ventricles, and then
                  CSF+other+unknown (from the *REN* files made by
                  AFNI/SUMA).

    qc_02*.jpg  : the overlay is the "2000" atlas parcellation (from
                  the file: aparc+aseg*REN*all*)

EXAMPLE ~1~

  adjunct_suma_fs_qc.tcsh                 \
      -sid       sub-001                  \
      -suma_dir  group/sub-001/SUMA

EOF

# ----------------------------------------------------------------------

    goto GOOD_EXIT

SHOW_VERSION:
   echo "version  $version (${rev_dat})"
   goto GOOD_EXIT

FAIL_MISSING_ARG:
    echo "** ERROR! Missing an argument after option flag: '$argv[$ac]'"
    goto BAD_EXIT

BAD_EXIT:
    exit 1

GOOD_EXIT:
    exit 0
