/*
  This is the header file for matrix.c.

  File:     matrix.h
  Author:   B. Douglas Ward
  Date:     23 April 1997

  Mod:      Added routines matrix_file_write and matrix_file_read.
  Date:     02 July 1999

*/



/*****************************************************************************
  This software is copyrighted and owned by the Medical College of Wisconsin.
  See the file README.Copyright for details.
******************************************************************************/

/*---------------------------------------------------------------------------*/
/*
  Define matrix and vector data structures.
*/


typedef struct matrix
{
  int      rows;
  int      cols;
  double ** elts;
}  matrix;


typedef struct vector
{
  int  dim;
  double * elts;
} vector;


/*---------------------------------------------------------------------------*/
/*
  Routine to print an error message and stop.
*/

void matrix_error (char * message);


/*---------------------------------------------------------------------------*/
/*
  Initialize matrix data structure.
*/

void matrix_initialize (matrix * m);


/*---------------------------------------------------------------------------*/
/*
  Destroy matrix data structure by deallocating memory.
*/

void matrix_destroy (matrix * m);


/*---------------------------------------------------------------------------*/
/*
  Create matrix data structure by allocating memory and initializing values.
*/

void matrix_create (int rows, int cols, matrix * m);


/*---------------------------------------------------------------------------*/
/*
  Print contents of matrix m.
*/

void matrix_print (matrix m);


/*---------------------------------------------------------------------------*/
/*
  Print contents of matrix m to specified file.
*/

void matrix_file_write (char * filename, matrix m);


/*---------------------------------------------------------------------------*/
/*
  Manual entry of matrix data.
*/

void matrix_enter (matrix * m);


/*---------------------------------------------------------------------------*/
/*
  Read contents of matrix m from specified file.
  Return null matrix if unable to read matrix from file.
*/

void matrix_file_read (char * filename, int rows, int cols,  matrix * m);


/*---------------------------------------------------------------------------*/
/*
  Convert simple array to matrix structure.
*/

void array_to_matrix (int rows, int cols, float ** f, matrix * m);


/*---------------------------------------------------------------------------*/
/*
  Make a copy of the first matrix, return copy as the second matrix.
*/

void matrix_equate (matrix a, matrix * b);


/*---------------------------------------------------------------------------*/
/*
  Extract p columns (specified by list) from matrix a.  Result is matrix b.
*/

void matrix_extract (matrix a, int p, int * list, matrix * b);


/*---------------------------------------------------------------------------*/
/*
  Create n x n identity matrix.
*/

void matrix_identity (int n, matrix * m);


/*---------------------------------------------------------------------------*/
/*
  Add matrix a to matrix b.  Result is matrix c.
*/

void matrix_add (matrix a, matrix b, matrix * c);


/*---------------------------------------------------------------------------*/
/*
  Subtract matrix b from matrix a.  Result is matrix c.
*/

void matrix_subtract (matrix a, matrix b, matrix * c);


/*---------------------------------------------------------------------------*/
/*
  Multiply matrix a by matrix b.  Result is matrix c.
*/

void matrix_multiply (matrix a, matrix b, matrix * c);


/*---------------------------------------------------------------------------*/
/*
  Multiply matrix a by scalar constant k.  Result is matrix c.
*/

void matrix_scale (double k, matrix a, matrix * c);


/*---------------------------------------------------------------------------*/
/*
  Take transpose of matrix a.  Result is matrix t.
*/

void matrix_transpose (matrix a, matrix * t);

 
/*---------------------------------------------------------------------------*/
/*
  Use Gaussian elimination to calculate inverse of matrix a.  Result is 
  matrix ainv.
*/

int matrix_inverse (matrix a, matrix * ainv);


/*---------------------------------------------------------------------------*/
/*
  Initialize vector data structure.
*/

void vector_initialize (vector * v);


/*---------------------------------------------------------------------------*/
/*
  Destroy vector data structure by deallocating memory.
*/

void vector_destroy (vector * v);


/*---------------------------------------------------------------------------*/
/*
  Create vector v by allocating memory and initializing values.
*/

void vector_create (int dim, vector * v);


/*---------------------------------------------------------------------------*/
/*
  Print contents of vector v.
*/

void vector_print (vector v);


/*---------------------------------------------------------------------------*/
/*
  Copy vector a.  Result is vector b.
*/

void vector_equate (vector a, vector * b);


/*---------------------------------------------------------------------------*/
/*
  Convert simple array f into vector v.
*/

void array_to_vector (int dim, float * f, vector * v);


/*---------------------------------------------------------------------------*/
/*
  Convert vector v into array f.
*/

void vector_to_array (vector v, float * f);


/*---------------------------------------------------------------------------*/
/*
  Add vector a to vector b.  Result is vector c.
*/

void vector_add (vector a, vector b, vector * c);


/*---------------------------------------------------------------------------*/
/*
  Subtract vector b from vector a.  Result is vector c.
*/

void vector_subtract (vector a, vector b, vector * c);


/*---------------------------------------------------------------------------*/
/*
  Right multiply matrix a by vector b.  Result is vector c.
*/

void vector_multiply (matrix a, vector b, vector * c);


/*---------------------------------------------------------------------------*/
/*
  Calculate dot product of vector a with vector b. 
*/

float vector_dot (vector a, vector b);


/*---------------------------------------------------------------------------*/


