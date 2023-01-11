#!/usr/bin/env python

# python3 status: compatible

# -------------------------------------------------------------------------
#
# Work in Progress
# Goals:
# - Ingest a root BIDS directory and be capable of IDing the contents of its
#   leaves.
# - Use this library and a set of afni_proc options to generate a
#   collection-wide set of afni_proc scripts.
#
# TODO: understand what PT means here
# The means for *getting* information are not going to be used
# longterm.  A BIDS expert will know better approaches for that aspect.
#
# -------------------------------------------------------------------------
#
# This basically comprises a set of objects to contain the
# (conceptual) data hierarchy:
# 
# collection
# |----subject (subj: sub-001, sub-002, ...)
#      |----ses (ses: ses-01, ses-02, ...; also sometimes just 'ses')
#           |----modality (AKA modality; datype: anat, func, ...)
#                |----twigs (datafiles with the same prefix)
#                     |----leaves (individual files)
#
# Here, these are represented as nested dictionaries, with each set of
# keys being the items of the next level down, until one gets to
# twigs, which allow you to generate the filenames of the leaves but do not
# directly store the filenames (this would waste memory since they only differ
# by extensions).
#
# In *many* cases, the hierarchical level is also the name of a
# directory containing other objects, until one reaches the
# 'data_files', which are just 'leaf' items.  However, the 'ses' level
# might not exist explicitly, breaking the concept-directory duality.
# We deal with that here by labelling such as a session as 'ses', with
# no hyphen, in the dictionary of hierarchies.
#
# -------------------------------------------------------------------------

import sys, os, glob
import subprocess
import argparse

from archivotron import bids
BIDS_GENERATOR = bids.generate_bids()

INDENT_STR = ' '*4

POSSIBLE_EXTENSIONS = (
    '.nii.gz',
    '.nii',
    '.json',
    '.HEAD',
)

LIB_AP_BIDS_VERBOSITY = 0

# -------------------------------------------------------------------------

def indent_str_by_line(s):
    """
    Parameter
    ---------
    s     : str
            A string s, which can contain 0 or more newline chars.

    Return
    ------
          : str
            A string with INDENT_STR prepended to the start of each 
            'line' in the input str.

    """

    if not s :
        return ''

    all_line = s.split('\n')
    return '\n'.join([INDENT_STR + line for line in all_line])

# -------------------------------------------------------------------------

def make_abspath_from_dirname(dirname):
    '''
    Parameters
    ----------
    dirname : str
              relative or abs path

    str
              abs path name

    '''
    if not os.path.isdir(dirname) :
        # TODO: make this a UserWarning?
        print("** WARN: {} is not a valid dirname".format(dirname))
        return ''
    return os.path.abspath(dirname)

def make_tail_from_dirname(dirname):
    '''
    Parameters
    ----------
    dirname : str
              relative or abs path

    tail    : str
              tail of abs path (i.e., last item in path)

    '''
    if not os.path.isdir(dirname) :
        # TODO: make this a UserWarning?
        print("+* WARN: {} is not a valid dirname".format(dirname))
        return ''

    apath = os.path.abspath(dirname)
    tail  = apath.split('/')[-1]

    if not len(tail):
        # TODO: make this a UserWarning?
        print(
            "+* WARN: cannot find tail from dir: {}\n"
            "   abspath = {}".format(dirname, apath)
        )
        return ''
    return tail

# -------------------------------------------------------------------------

class Collection:
    """An object storing the full data collection."""
    def __init__(self, dirname: str, label: str = '', verb: int = 0):
        """A collection of data which follows a well-defined rubric.

        Parameters
        ----------
        dirname: str
            The directory that acts as the collection root.
        label: str, optional
            The label to give this dataset. Default ''.
        """
        dirname = dirname.rstrip('/')          # remove any trailing '/'

        self.dirname = dirname                # entered path
        self.abspath = make_abspath_from_dirname(dirname)
        self.collection_name = make_tail_from_dirname(dirname)
        self.label   = label                  # dataset/proj label

        # glob for all sub-* directories in self.abspath
        all_dir = [ x for x in glob.glob( self.abspath + '/sub-*' ) \
                     if os.path.isdir(x) ]

        self.subj_dict = {
            make_tail_from_dirname(s): Subject(s) for s in all_dir
        }

        self.verb          = verb

    @property
    def subj_list(self) -> list:
        """
        Return list of session IDs.
        """
        lll = list(self.subj_dict.keys())
        lll.sort()
        return lll

    def __str__(self) -> str:
        """Return a (multiline) string of the data collection contents,
        hierarchically indenting by each level (kinda like tree).

        """

        lines = []
        for subj in self.subj_list :
            subj_obj   = self.subj_dict[subj]
            lines.append(subj_obj.get_subj)
            lines.append(indent_str_by_line(str(subj_obj)))
        return '\n'.join(lines)



class Subject:
    """An object storing a collection subject.

    A subject always contains sessions, but session directories may not be
    present in the actual directory layout!
    We refer to this as an "unseparated"
    session, and it will just bear the label 'ses' (rather than 'ses-*').

    """

    def __init__(self, dirname):
        dirname = dirname.rstrip('/')          # remove any trailing '/'

        self.dirname  = dirname                # entered path
        self.abspath  = make_abspath_from_dirname(dirname)
        self.subj     = make_tail_from_dirname(dirname)
        self.ses_dict = {}                     # dict of sessions

        # glob for all ses-* directories in self.abspath
        contents_with_ses = [ x for x in glob.glob( self.abspath + '/ses-*' ) \
                              if os.path.isdir(x) ]

        if len(contents_with_ses) == 0:
            # Invisible session, make a session from the same dir
            self.ses_dict = {"ses": Session(self.abspath)}
        else:
            self.ses_dict = {
                make_tail_from_dirname(s): Session(s) for s in contents_with_ses
            }

    @property
    def get_subj(self) -> str:
        """
        Return subject ID (in a safe way!)
        """
        return self.subj

    @property
    def ses_list(self) -> list:
        """
        Return list of session IDs.
        """
        lll = list(self.ses_dict.keys())
        lll.sort()
        return lll

    def __str__(self) -> str:
        ses_strings = []
        for ses, ses_obj in self.ses_dict.items():
            ses_strings.append('{}:'.format(ses))
            ses_lines = [
                '\t' + str(sl) for sl in str(ses_obj).split('\n')
            ]
            ses_strings.append('\n'.join(ses_lines))
        return '\n'.join(ses_strings)
    
class Session:
    """An object storing the session-level information for a subject.

    Importantly, contains a dictionary of modality info.
    """

    def __init__(self, dirname):
        dirname        = dirname.rstrip('/')

        self.dirname  = dirname                # directory
        self.abspath  = make_abspath_from_dirname(dirname)
        tail_dirname  = make_tail_from_dirname(dirname)
        self.ses      = tail_dirname if "ses" in tail_dirname else "ses"
        self.modality_dict = {}

        # glob for all directories in self.abspath
        all_dir = [ x for x in glob.glob( self.abspath + '/*' ) \
                     if os.path.isdir(x) ]

        self.modality_dict = {
            make_tail_from_dirname(m): Modality(m) for m in all_dir
        }

    @property
    def subject_label(self) -> str:
        return self.modality_dict[list(self.modality_dict.keys())[0]].subject

    @property
    def session_label(self) -> str:
        return self.modality_dict[list(self.modality_dict.keys())[0]].session

    @property
    def modality_list(self) -> list:
        """
        Return (lexicographically sorted) list of modalities
        """
        lll = list(self.modality_dict.keys())
        lll.sort()

        return lll

    def __str__(self) -> str:
        lines = []
        for modality in self.modality_list :
            modality_obj = self.modality_dict[modality]
            lines.append(modality_obj.get_modality)
            lines.append(indent_str_by_line(str(modality_obj)))
        return '\n'.join(lines)

              
class Modality:
    """An object storing the BIDS modality (or in BIDS parlance, "data type")
    -level (contents of anat/, func/, etc.) information for a subject.

    Rather than more directories, this object contains "twigs" which bundle
    files that share prefixes.
    """

    def __init__(self, dirname):
        dirname       = dirname.rstrip('/')

        self.dirname  = dirname                # directory
        self.abspath  = make_abspath_from_dirname(dirname)

        name_ext = {}
        for f in glob.glob(self.dirname + '/*'):
            for x in POSSIBLE_EXTENSIONS:
                if f.endswith(x):
                    f = f.rstrip(x)
                    if f in name_ext.keys():
                        name_ext[f].append(x)
                    else:
                        name_ext[f] = [x]
        self.list_twig = [
            DataTwig(self.dirname, k, name_ext[k]) for k in name_ext.keys()
        ]

    @property
    def subject(self) -> str:
        return self.list_twig[0].subject

    @property
    def session(self) -> str:
        return self.list_twig[0].session

    @property
    def get_modality(self) -> str:
        return self.list_twig[0].modality

    def __str__(self) -> str:
        return ', '.join([str(t) for t in self.list_twig])


class DataTwig:
    """A collection of closely related data files which share the same prefix."""
    def __init__(self,
        dirname: str,
        prefix: str,
        extensions: list,
    ):
        """Construct a DataTwig

        Parameters
        ----------
        dirname: str
            The relative path from the root of the collection to this twig's parent.
        prefix: str
            The common prefix for all files in this twig.
        extensions: list
            The available file extensions for this twig.
        """
        self.dirname = dirname
        self.prefix = prefix
        self.list_ext = extensions
        self.arch_info = BIDS_GENERATOR.into_attributes(
            self.dirname + '/' + self.prefix
        )

    @property
    def leaves(self) -> list:
        return [
            self.dirname + '/' + self.prefix + ext for ext in self.list_ext
        ]

    @property
    def subject(self) -> str:
        return self.arch_info['sub']

    @property
    def session(self) -> str:
        if "ses" in self.arch_info.keys():
            return self.arch_info['ses']
        else:
            return "ses"

    @property
    def modality(self) -> str:
        return self.arch_info['modality']

    def __str__(self) -> str:
        return self.arch_info['suffix']


# TODO: break this into its own script
def main():
    parser = argparse.ArgumentParser(
        prog='arco',
        description='Investigate a dataset structure'
    )
    parser.add_argument(
        'dataset',
        help='The dataset to investigate',
    )
    args = parser.parse_args()
    dataset = os.path.relpath(args.dataset)
    bd = Collection(dataset)
    print(bd)

if __name__ == '__main__':
    main()
