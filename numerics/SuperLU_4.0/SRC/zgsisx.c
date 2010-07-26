
/*! @file zgsisx.c
 * \brief Gives the approximate solutions of linear equations A*X=B or A'*X=B
 *
 * <pre>
 * -- SuperLU routine (version 4.0) --
 * Lawrence Berkeley National Laboratory.
 * June 30, 2009
 * </pre>
 */
#include "slu_zdefs.h"

/*! \brief
 *
 * <pre>
 * Purpose
 * =======
 *
 * ZGSISX gives the approximate solutions of linear equations A*X=B or A'*X=B,
 * using the ILU factorization from zgsitrf(). An estimation of
 * the condition number is provided. It performs the following steps:
 *
 *   1. If A is stored column-wise (A->Stype = SLU_NC):
 *  
 *	1.1. If options->Equil = YES or options->RowPerm = LargeDiag, scaling
 *	     factors are computed to equilibrate the system:
 *	     options->Trans = NOTRANS:
 *		 diag(R)*A*diag(C) *inv(diag(C))*X = diag(R)*B
 *	     options->Trans = TRANS:
 *		 (diag(R)*A*diag(C))**T *inv(diag(R))*X = diag(C)*B
 *	     options->Trans = CONJ:
 *		 (diag(R)*A*diag(C))**H *inv(diag(R))*X = diag(C)*B
 *	     Whether or not the system will be equilibrated depends on the
 *	     scaling of the matrix A, but if equilibration is used, A is
 *	     overwritten by diag(R)*A*diag(C) and B by diag(R)*B
 *	     (if options->Trans=NOTRANS) or diag(C)*B (if options->Trans
 *	     = TRANS or CONJ).
 *
 *	1.2. Permute columns of A, forming A*Pc, where Pc is a permutation
 *	     matrix that usually preserves sparsity.
 *	     For more details of this step, see sp_preorder.c.
 *
 *	1.3. If options->Fact != FACTORED, the LU decomposition is used to
 *	     factor the matrix A (after equilibration if options->Equil = YES)
 *	     as Pr*A*Pc = L*U, with Pr determined by partial pivoting.
 *
 *	1.4. Compute the reciprocal pivot growth factor.
 *
 *	1.5. If some U(i,i) = 0, so that U is exactly singular, then the
 *	     routine fills a small number on the diagonal entry, that is
 *		U(i,i) = ||A(:,i)||_oo * options->ILU_FillTol ** (1 - i / n),
 *	     and info will be increased by 1. The factored form of A is used
 *	     to estimate the condition number of the preconditioner. If the
 *	     reciprocal of the condition number is less than machine precision,
 *	     info = A->ncol+1 is returned as a warning, but the routine still
 *	     goes on to solve for X.
 *
 *	1.6. The system of equations is solved for X using the factored form
 *	     of A.
 *
 *	1.7. options->IterRefine is not used
 *
 *	1.8. If equilibration was used, the matrix X is premultiplied by
 *	     diag(C) (if options->Trans = NOTRANS) or diag(R)
 *	     (if options->Trans = TRANS or CONJ) so that it solves the
 *	     original system before equilibration.
 *
 *	1.9. options for ILU only
 *	     1) If options->RowPerm = LargeDiag, MC64 is used to scale and
 *		permute the matrix to an I-matrix, that is Pr*Dr*A*Dc has
 *		entries of modulus 1 on the diagonal and off-diagonal entries
 *		of modulus at most 1. If MC64 fails, dgsequ() is used to
 *		equilibrate the system.
 *	     2) options->ILU_DropTol = tau is the threshold for dropping.
 *		For L, it is used directly (for the whole row in a supernode);
 *		For U, ||A(:,i)||_oo * tau is used as the threshold
 *	        for the	i-th column.
 *		If a secondary dropping rule is required, tau will
 *	        also be used to compute the second threshold.
 *	     3) options->ILU_FillFactor = gamma, used as the initial guess
 *		of memory growth.
 *		If a secondary dropping rule is required, it will also
 *              be used as an upper bound of the memory.
 *	     4) options->ILU_DropRule specifies the dropping rule.
 *		Option		Explanation
 *		======		===========
 *		DROP_BASIC:	Basic dropping rule, supernodal based ILU.
 *		DROP_PROWS:	Supernodal based ILUTP, p = gamma * nnz(A) / n.
 *		DROP_COLUMN:	Variation of ILUTP, for j-th column,
 *				p = gamma * nnz(A(:,j)).
 *		DROP_AREA;	Variation of ILUTP, for j-th column, use
 *				nnz(F(:,1:j)) / nnz(A(:,1:j)) to control the
 *				memory.
 *		DROP_DYNAMIC:	Modify the threshold tau during the
 *				factorizaion.
 *				If nnz(L(:,1:j)) / nnz(A(:,1:j)) < gamma
 *				    tau_L(j) := MIN(1, tau_L(j-1) * 2);
 *				Otherwise
 *				    tau_L(j) := MIN(1, tau_L(j-1) * 2);
 *				tau_U(j) uses the similar rule.
 *				NOTE: the thresholds used by L and U are
 *				indenpendent.
 *		DROP_INTERP:	Compute the second dropping threshold by
 *				interpolation instead of sorting (default).
 *				In this case, the actual fill ratio is not
 *				guaranteed smaller than gamma.
 *		DROP_PROWS, DROP_COLUMN and DROP_AREA are mutually exclusive.
 *		( The default option is DROP_BASIC | DROP_AREA. )
 *	     5) options->ILU_Norm is the criterion of computing the average
 *		value of a row in L.
 *		options->ILU_Norm	average(x[1:n])
 *		=================	===============
 *		ONE_NORM		||x||_1 / n
 *		TWO_NORM		||x||_2 / sqrt(n)
 *		INF_NORM		max{|x[i]|}
 *	     6) options->ILU_MILU specifies the type of MILU's variation.
 *		= SILU (default): do not perform MILU;
 *		= SMILU_1 (not recommended):
 *		    U(i,i) := U(i,i) + sum(dropped entries);
 *		= SMILU_2:
 *		    U(i,i) := U(i,i) + SGN(U(i,i)) * sum(dropped entries);
 *		= SMILU_3:
 *		    U(i,i) := U(i,i) + SGN(U(i,i)) * sum(|dropped entries|);
 *		NOTE: Even SMILU_1 does not preserve the column sum because of
 *		late dropping.
 *	     7) options->ILU_FillTol is used as the perturbation when
 *		encountering zero pivots. If some U(i,i) = 0, so that U is
 *		exactly singular, then
 *		   U(i,i) := ||A(:,i)|| * options->ILU_FillTol ** (1 - i / n).
 *
 *   2. If A is stored row-wise (A->Stype = SLU_NR), apply the above algorithm
 *	to the transpose of A:
 *
 *	2.1. If options->Equil = YES or options->RowPerm = LargeDiag, scaling
 *	     factors are computed to equilibrate the system:
 *	     options->Trans = NOTRANS:
 *		 diag(R)*A*diag(C) *inv(diag(C))*X = diag(R)*B
 *	     options->Trans = TRANS:
 *		 (diag(R)*A*diag(C))**T *inv(diag(R))*X = diag(C)*B
 *	     options->Trans = CONJ:
 *		 (diag(R)*A*diag(C))**H *inv(diag(R))*X = diag(C)*B
 *	     Whether or not the system will be equilibrated depends on the
 *	     scaling of the matrix A, but if equilibration is used, A' is
 *	     overwritten by diag(R)*A'*diag(C) and B by diag(R)*B
 *	     (if trans='N') or diag(C)*B (if trans = 'T' or 'C').
 *
 *	2.2. Permute columns of transpose(A) (rows of A),
 *	     forming transpose(A)*Pc, where Pc is a permutation matrix that
 *	     usually preserves sparsity.
 *	     For more details of this step, see sp_preorder.c.
 *
 *	2.3. If options->Fact != FACTORED, the LU decomposition is used to
 *	     factor the transpose(A) (after equilibration if
 *	     options->Fact = YES) as Pr*transpose(A)*Pc = L*U with the
 *	     permutation Pr determined by partial pivoting.
 *
 *	2.4. Compute the reciprocal pivot growth factor.
 *
 *	2.5. If some U(i,i) = 0, so that U is exactly singular, then the
 *	     routine fills a small number on the diagonal entry, that is
 *		 U(i,i) = ||A(:,i)||_oo * options->ILU_FillTol ** (1 - i / n).
 *	     And info will be increased by 1. The factored form of A is used
 *	     to estimate the condition number of the preconditioner. If the
 *	     reciprocal of the condition number is less than machine precision,
 *	     info = A->ncol+1 is returned as a warning, but the routine still
 *	     goes on to solve for X.
 *
 *	2.6. The system of equations is solved for X using the factored form
 *	     of transpose(A).
 *
 *	2.7. If options->IterRefine is not used.
 *
 *	2.8. If equilibration was used, the matrix X is premultiplied by
 *	     diag(C) (if options->Trans = NOTRANS) or diag(R)
 *	     (if options->Trans = TRANS or CONJ) so that it solves the
 *	     original system before equilibration.
 *
 *   See supermatrix.h for the definition of 'SuperMatrix' structure.
 *
 * Arguments
 * =========
 *
 * options (input) superlu_options_t*
 *	   The structure defines the input parameters to control
 *	   how the LU decomposition will be performed and how the
 *	   system will be solved.
 *
 * A	   (input/output) SuperMatrix*
 *	   Matrix A in A*X=B, of dimension (A->nrow, A->ncol). The number
 *	   of the linear equations is A->nrow. Currently, the type of A can be:
 *	   Stype = SLU_NC or SLU_NR, Dtype = SLU_Z, Mtype = SLU_GE.
 *	   In the future, more general A may be handled.
 *
 *	   On entry, If options->Fact = FACTORED and equed is not 'N',
 *	   then A must have been equilibrated by the scaling factors in
 *	   R and/or C.
 *	   On exit, A is not modified if options->Equil = NO, or if
 *	   options->Equil = YES but equed = 'N' on exit.
 *	   Otherwise, if options->Equil = YES and equed is not 'N',
 *	   A is scaled as follows:
 *	   If A->Stype = SLU_NC:
 *	     equed = 'R':  A := diag(R) * A
 *	     equed = 'C':  A := A * diag(C)
 *	     equed = 'B':  A := diag(R) * A * diag(C).
 *	   If A->Stype = SLU_NR:
 *	     equed = 'R':  transpose(A) := diag(R) * transpose(A)
 *	     equed = 'C':  transpose(A) := transpose(A) * diag(C)
 *	     equed = 'B':  transpose(A) := diag(R) * transpose(A) * diag(C).
 *
 * perm_c  (input/output) int*
 *	   If A->Stype = SLU_NC, Column permutation vector of size A->ncol,
 *	   which defines the permutation matrix Pc; perm_c[i] = j means
 *	   column i of A is in position j in A*Pc.
 *	   On exit, perm_c may be overwritten by the product of the input
 *	   perm_c and a permutation that postorders the elimination tree
 *	   of Pc'*A'*A*Pc; perm_c is not changed if the elimination tree
 *	   is already in postorder.
 *
 *	   If A->Stype = SLU_NR, column permutation vector of size A->nrow,
 *	   which describes permutation of columns of transpose(A) 
 *	   (rows of A) as described above.
 *
 * perm_r  (input/output) int*
 *	   If A->Stype = SLU_NC, row permutation vector of size A->nrow, 
 *	   which defines the permutation matrix Pr, and is determined
 *	   by partial pivoting.  perm_r[i] = j means row i of A is in 
 *	   position j in Pr*A.
 *
 *	   If A->Stype = SLU_NR, permutation vector of size A->ncol, which
 *	   determines permutation of rows of transpose(A)
 *	   (columns of A) as described above.
 *
 *	   If options->Fact = SamePattern_SameRowPerm, the pivoting routine
 *	   will try to use the input perm_r, unless a certain threshold
 *	   criterion is violated. In that case, perm_r is overwritten by a
 *	   new permutation determined by partial pivoting or diagonal
 *	   threshold pivoting.
 *	   Otherwise, perm_r is output argument.
 *
 * etree   (input/output) int*,  dimension (A->ncol)
 *	   Elimination tree of Pc'*A'*A*Pc.
 *	   If options->Fact != FACTORED and options->Fact != DOFACT,
 *	   etree is an input argument, otherwise it is an output argument.
 *	   Note: etree is a vector of parent pointers for a forest whose
 *	   vertices are the integers 0 to A->ncol-1; etree[root]==A->ncol.
 *
 * equed   (input/output) char*
 *	   Specifies the form of equilibration that was done.
 *	   = 'N': No equilibration.
 *	   = 'R': Row equilibration, i.e., A was premultiplied by diag(R).
 *	   = 'C': Column equilibration, i.e., A was postmultiplied by diag(C).
 *	   = 'B': Both row and column equilibration, i.e., A was replaced 
 *		  by diag(R)*A*diag(C).
 *	   If options->Fact = FACTORED, equed is an input argument,
 *	   otherwise it is an output argument.
 *
 * R	   (input/output) double*, dimension (A->nrow)
 *	   The row scale factors for A or transpose(A).
 *	   If equed = 'R' or 'B', A (if A->Stype = SLU_NC) or transpose(A)
 *	       (if A->Stype = SLU_NR) is multiplied on the left by diag(R).
 *	   If equed = 'N' or 'C', R is not accessed.
 *	   If options->Fact = FACTORED, R is an input argument,
 *	       otherwise, R is output.
 *	   If options->zFact = FACTORED and equed = 'R' or 'B', each element
 *	       of R must be positive.
 *
 * C	   (input/output) double*, dimension (A->ncol)
 *	   The column scale factors for A or transpose(A).
 *	   If equed = 'C' or 'B', A (if A->Stype = SLU_NC) or transpose(A)
 *	       (if A->Stype = SLU_NR) is multiplied on the right by diag(C).
 *	   If equed = 'N' or 'R', C is not accessed.
 *	   If options->Fact = FACTORED, C is an input argument,
 *	       otherwise, C is output.
 *	   If options->Fact = FACTORED and equed = 'C' or 'B', each element
 *	       of C must be positive.
 *
 * L	   (output) SuperMatrix*
 *	   The factor L from the factorization
 *	       Pr*A*Pc=L*U		(if A->Stype SLU_= NC) or
 *	       Pr*transpose(A)*Pc=L*U	(if A->Stype = SLU_NR).
 *	   Uses compressed row subscripts storage for supernodes, i.e.,
 *	   L has types: Stype = SLU_SC, Dtype = SLU_Z, Mtype = SLU_TRLU.
 *
 * U	   (output) SuperMatrix*
 *	   The factor U from the factorization
 *	       Pr*A*Pc=L*U		(if A->Stype = SLU_NC) or
 *	       Pr*transpose(A)*Pc=L*U	(if A->Stype = SLU_NR).
 *	   Uses column-wise storage scheme, i.e., U has types:
 *	   Stype = SLU_NC, Dtype = SLU_Z, Mtype = SLU_TRU.
 *
 * work    (workspace/output) void*, size (lwork) (in bytes)
 *	   User supplied workspace, should be large enough
 *	   to hold data structures for factors L and U.
 *	   On exit, if fact is not 'F', L and U point to this array.
 *
 * lwork   (input) int
 *	   Specifies the size of work array in bytes.
 *	   = 0:  allocate space internally by system malloc;
 *	   > 0:  use user-supplied work array of length lwork in bytes,
 *		 returns error if space runs out.
 *	   = -1: the routine guesses the amount of space needed without
 *		 performing the factorization, and returns it in
 *		 mem_usage->total_needed; no other side effects.
 *
 *	   See argument 'mem_usage' for memory usage statistics.
 *
 * B	   (input/output) SuperMatrix*
 *	   B has types: Stype = SLU_DN, Dtype = SLU_Z, Mtype = SLU_GE.
 *	   On entry, the right hand side matrix.
 *	   If B->ncol = 0, only LU decomposition is performed, the triangular
 *			   solve is skipped.
 *	   On exit,
 *	      if equed = 'N', B is not modified; otherwise
 *	      if A->Stype = SLU_NC:
 *		 if options->Trans = NOTRANS and equed = 'R' or 'B',
 *		    B is overwritten by diag(R)*B;
 *		 if options->Trans = TRANS or CONJ and equed = 'C' of 'B',
 *		    B is overwritten by diag(C)*B;
 *	      if A->Stype = SLU_NR:
 *		 if options->Trans = NOTRANS and equed = 'C' or 'B',
 *		    B is overwritten by diag(C)*B;
 *		 if options->Trans = TRANS or CONJ and equed = 'R' of 'B',
 *		    B is overwritten by diag(R)*B.
 *
 * X	   (output) SuperMatrix*
 *	   X has types: Stype = SLU_DN, Dtype = SLU_Z, Mtype = SLU_GE.
 *	   If info = 0 or info = A->ncol+1, X contains the solution matrix
 *	   to the original system of equations. Note that A and B are modified
 *	   on exit if equed is not 'N', and the solution to the equilibrated
 *	   system is inv(diag(C))*X if options->Trans = NOTRANS and
 *	   equed = 'C' or 'B', or inv(diag(R))*X if options->Trans = 'T' or 'C'
 *	   and equed = 'R' or 'B'.
 *
 * recip_pivot_growth (output) double*
 *	   The reciprocal pivot growth factor max_j( norm(A_j)/norm(U_j) ).
 *	   The infinity norm is used. If recip_pivot_growth is much less
 *	   than 1, the stability of the LU factorization could be poor.
 *
 * rcond   (output) double*
 *	   The estimate of the reciprocal condition number of the matrix A
 *	   after equilibration (if done). If rcond is less than the machine
 *	   precision (in particular, if rcond = 0), the matrix is singular
 *	   to working precision. This condition is indicated by a return
 *	   code of info > 0.
 *
 * mem_usage (output) mem_usage_t*
 *	   Record the memory usage statistics, consisting of following fields:
 *	   - for_lu (float)
 *	     The amount of space used in bytes for L\U data structures.
 *	   - total_needed (float)
 *	     The amount of space needed in bytes to perform factorization.
 *	   - expansions (int)
 *	     The number of memory expansions during the LU factorization.
 *
 * stat   (output) SuperLUStat_t*
 *	  Record the statistics on runtime and floating-point operation count.
 *	  See slu_util.h for the definition of 'SuperLUStat_t'.
 *
 * info    (output) int*
 *	   = 0: successful exit
 *	   < 0: if info = -i, the i-th argument had an illegal value
 *	   > 0: if info = i, and i is
 *		<= A->ncol: number of zero pivots. They are replaced by small
 *		      entries due to options->ILU_FillTol.
 *		= A->ncol+1: U is nonsingular, but RCOND is less than machine
 *		      precision, meaning that the matrix is singular to
 *		      working precision. Nevertheless, the solution and
 *		      error bounds are computed because there are a number
 *		      of situations where the computed solution can be more
 *		      accurate than the value of RCOND would suggest.
 *		> A->ncol+1: number of bytes allocated when memory allocation
 *		      failure occurred, plus A->ncol.
 * </pre>
 */

void
zgsisx(superlu_options_t *options, SuperMatrix *A, int *perm_c, int *perm_r,
       int *etree, char *equed, double *R, double *C,
       SuperMatrix *L, SuperMatrix *U, void *work, int lwork,
       SuperMatrix *B, SuperMatrix *X,
       double *recip_pivot_growth, double *rcond,
       mem_usage_t *mem_usage, SuperLUStat_t *stat, int *info)
{

    DNformat  *Bstore, *Xstore;
    doublecomplex    *Bmat, *Xmat;
    int       ldb, ldx, nrhs;
    SuperMatrix *AA;/* A in SLU_NC format used by the factorization routine.*/
    SuperMatrix AC; /* Matrix postmultiplied by Pc */
    int       colequ, equil, nofact, notran, rowequ, permc_spec, mc64;
    trans_t   trant;
    char      norm[1];
    int       i, j, info1;
    double    amax, anorm, bignum, smlnum, colcnd, rowcnd, rcmax, rcmin;
    int       relax, panel_size;
    double    diag_pivot_thresh;
    double    t0;      /* temporary time */
    double    *utime;

    int *perm = NULL;

    /* External functions */
    extern double zlangs(char *, SuperMatrix *);

    Bstore = B->Store;
    Xstore = X->Store;
    Bmat   = Bstore->nzval;
    Xmat   = Xstore->nzval;
    ldb    = Bstore->lda;
    ldx    = Xstore->lda;
    nrhs   = B->ncol;

    *info = 0;
    nofact = (options->Fact != FACTORED);
    equil = (options->Equil == YES);
    notran = (options->Trans == NOTRANS);
    mc64 = (options->RowPerm == LargeDiag);
    if ( nofact ) {
	*(unsigned char *)equed = 'N';
	rowequ = FALSE;
	colequ = FALSE;
    } else {
	rowequ = lsame_(equed, "R") || lsame_(equed, "B");
	colequ = lsame_(equed, "C") || lsame_(equed, "B");
	smlnum = dlamch_("Safe minimum");
	bignum = 1. / smlnum;
    }

    /* Test the input parameters */
    if (!nofact && options->Fact != DOFACT && options->Fact != SamePattern &&
	options->Fact != SamePattern_SameRowPerm &&
	!notran && options->Trans != TRANS && options->Trans != CONJ &&
	!equil && options->Equil != NO)
	*info = -1;
    else if ( A->nrow != A->ncol || A->nrow < 0 ||
	      (A->Stype != SLU_NC && A->Stype != SLU_NR) ||
	      A->Dtype != SLU_Z || A->Mtype != SLU_GE )
	*info = -2;
    else if (options->Fact == FACTORED &&
	     !(rowequ || colequ || lsame_(equed, "N")))
	*info = -6;
    else {
	if (rowequ) {
	    rcmin = bignum;
	    rcmax = 0.;
	    for (j = 0; j < A->nrow; ++j) {
		rcmin = SUPERLU_MIN(rcmin, R[j]);
		rcmax = SUPERLU_MAX(rcmax, R[j]);
	    }
	    if (rcmin <= 0.) *info = -7;
	    else if ( A->nrow > 0)
		rowcnd = SUPERLU_MAX(rcmin,smlnum) / SUPERLU_MIN(rcmax,bignum);
	    else rowcnd = 1.;
	}
	if (colequ && *info == 0) {
	    rcmin = bignum;
	    rcmax = 0.;
	    for (j = 0; j < A->nrow; ++j) {
		rcmin = SUPERLU_MIN(rcmin, C[j]);
		rcmax = SUPERLU_MAX(rcmax, C[j]);
	    }
	    if (rcmin <= 0.) *info = -8;
	    else if (A->nrow > 0)
		colcnd = SUPERLU_MAX(rcmin,smlnum) / SUPERLU_MIN(rcmax,bignum);
	    else colcnd = 1.;
	}
	if (*info == 0) {
	    if ( lwork < -1 ) *info = -12;
	    else if ( B->ncol < 0 || Bstore->lda < SUPERLU_MAX(0, A->nrow) ||
		      B->Stype != SLU_DN || B->Dtype != SLU_Z || 
		      B->Mtype != SLU_GE )
		*info = -13;
	    else if ( X->ncol < 0 || Xstore->lda < SUPERLU_MAX(0, A->nrow) ||
		      (B->ncol != 0 && B->ncol != X->ncol) ||
		      X->Stype != SLU_DN ||
		      X->Dtype != SLU_Z || X->Mtype != SLU_GE )
		*info = -14;
	}
    }
    if (*info != 0) {
	i = -(*info);
	xerbla_("zgsisx", &i);
	return;
    }

    /* Initialization for factor parameters */
    panel_size = sp_ienv(1);
    relax      = sp_ienv(2);
    diag_pivot_thresh = options->DiagPivotThresh;

    utime = stat->utime;

    /* Convert A to SLU_NC format when necessary. */
    if ( A->Stype == SLU_NR ) {
	NRformat *Astore = A->Store;
	AA = (SuperMatrix *) SUPERLU_MALLOC( sizeof(SuperMatrix) );
	toast_zCreate_CompCol_Matrix(AA, A->ncol, A->nrow, Astore->nnz,
			       Astore->nzval, Astore->colind, Astore->rowptr,
			       SLU_NC, A->Dtype, A->Mtype);
	if ( notran ) { /* Reverse the transpose argument. */
	    trant = TRANS;
	    notran = 0;
	} else {
	    trant = NOTRANS;
	    notran = 1;
	}
    } else { /* A->Stype == SLU_NC */
	trant = options->Trans;
	AA = A;
    }

    if ( nofact ) {
	register int i, j;
	NCformat *Astore = AA->Store;
	int nnz = Astore->nnz;
	int *colptr = Astore->colptr;
	int *rowind = Astore->rowind;
	doublecomplex *nzval = (doublecomplex *)Astore->nzval;
	int n = AA->nrow;

	if ( mc64 ) {
	    *equed = 'B';
	    rowequ = colequ = 1;
	    t0 = SuperLU_timer_();
	    if ((perm = intMalloc(n)) == NULL)
		ABORT("SUPERLU_MALLOC fails for perm[]");

	    info1 = zldperm(5, n, nnz, colptr, rowind, nzval, perm, R, C);

	    if (info1 > 0) { /* MC64 fails, call zgsequ() later */
		mc64 = 0;
		SUPERLU_FREE(perm);
		perm = NULL;
	    } else {
		for (i = 0; i < n; i++) {
		    R[i] = exp(R[i]);
		    C[i] = exp(C[i]);
		}
		/* permute and scale the matrix */
		for (j = 0; j < n; j++) {
		    for (i = colptr[j]; i < colptr[j + 1]; i++) {
                        zd_mult(&nzval[i], &nzval[i], R[rowind[i]] * C[j]);
			rowind[i] = perm[rowind[i]];
		    }
		}
	    }
	    utime[EQUIL] = SuperLU_timer_() - t0;
	}
	if ( !mc64 & equil ) {
	    t0 = SuperLU_timer_();
	    /* Compute row and column scalings to equilibrate the matrix A. */
	    zgsequ(AA, R, C, &rowcnd, &colcnd, &amax, &info1);

	    if ( info1 == 0 ) {
		/* Equilibrate matrix A. */
		zlaqgs(AA, R, C, rowcnd, colcnd, amax, equed);
		rowequ = lsame_(equed, "R") || lsame_(equed, "B");
		colequ = lsame_(equed, "C") || lsame_(equed, "B");
	    }
	    utime[EQUIL] = SuperLU_timer_() - t0;
	}
    }

    if ( nrhs > 0 ) {
	/* Scale the right hand side if equilibration was performed. */
	if ( notran ) {
	    if ( rowequ ) {
		for (j = 0; j < nrhs; ++j)
		    for (i = 0; i < A->nrow; ++i) {
                        zd_mult(&Bmat[i+j*ldb], &Bmat[i+j*ldb], R[i]);
		    }
	    }
	} else if ( colequ ) {
	    for (j = 0; j < nrhs; ++j)
		for (i = 0; i < A->nrow; ++i) {
                    zd_mult(&Bmat[i+j*ldb], &Bmat[i+j*ldb], C[i]);
		}
	}
    }

    if ( nofact ) {
	
	t0 = SuperLU_timer_();
	/*
	 * Gnet column permutation vector perm_c[], according to permc_spec:
	 *   permc_spec = NATURAL:  natural ordering 
	 *   permc_spec = MMD_AT_PLUS_A: minimum degree on structure of A'+A
	 *   permc_spec = MMD_ATA:  minimum degree on structure of A'*A
	 *   permc_spec = COLAMD:   approximate minimum degree column ordering
	 *   permc_spec = MY_PERMC: the ordering already supplied in perm_c[]
	 */
	permc_spec = options->ColPerm;
	if ( permc_spec != MY_PERMC && options->Fact == DOFACT )
	    toast_get_perm_c(permc_spec, AA, perm_c);
	utime[COLPERM] = SuperLU_timer_() - t0;

	t0 = SuperLU_timer_();
	sp_preorder(options, AA, perm_c, etree, &AC);
	utime[ETREE] = SuperLU_timer_() - t0;

	/* Compute the LU factorization of A*Pc. */
	t0 = SuperLU_timer_();
	zgsitrf(options, &AC, relax, panel_size, etree, work, lwork,
                perm_c, perm_r, L, U, stat, info);
	utime[FACT] = SuperLU_timer_() - t0;

	if ( lwork == -1 ) {
	    mem_usage->total_needed = *info - A->ncol;
	    return;
	}
    }

    if ( options->PivotGrowth ) {
	if ( *info > 0 ) return;

	/* Compute the reciprocal pivot growth factor *recip_pivot_growth. */
	*recip_pivot_growth = zPivotGrowth(A->ncol, AA, perm_c, L, U);
    }

    if ( options->ConditionNumber ) {
	/* Estimate the reciprocal of the condition number of A. */
	t0 = SuperLU_timer_();
	if ( notran ) {
	    *(unsigned char *)norm = '1';
	} else {
	    *(unsigned char *)norm = 'I';
	}
	anorm = zlangs(norm, AA);
	zgscon(norm, L, U, anorm, rcond, stat, &info1);
	utime[RCOND] = SuperLU_timer_() - t0;
    }

    if ( nrhs > 0 ) {
	/* Compute the solution matrix X. */
	for (j = 0; j < nrhs; j++)  /* Save a copy of the right hand sides */
	    for (i = 0; i < B->nrow; i++)
		Xmat[i + j*ldx] = Bmat[i + j*ldb];

	t0 = SuperLU_timer_();
	zgstrs (trant, L, U, perm_c, perm_r, X, stat, &info1);
	utime[SOLVE] = SuperLU_timer_() - t0;

	/* Transform the solution matrix X to a solution of the original
	   system. */
	if ( notran ) {
	    if ( colequ ) {
		for (j = 0; j < nrhs; ++j)
		    for (i = 0; i < A->nrow; ++i) {
                        zd_mult(&Xmat[i+j*ldx], &Xmat[i+j*ldx], C[i]);
                    }
	    }
	} else {
	    if ( rowequ ) {
		if (perm) {
		    doublecomplex *tmp;
		    int n = A->nrow;

                    if ((tmp = toast_doublecomplexMalloc(n)) == NULL)
			ABORT("SUPERLU_MALLOC fails for tmp[]");
		    for (j = 0; j < nrhs; j++) {
			for (i = 0; i < n; i++)
			    tmp[i] = Xmat[i + j * ldx]; /*dcopy*/
			for (i = 0; i < n; i++)
                           zd_mult(&Xmat[i+j*ldx], &tmp[perm[i]], R[i]);
		    }
		    SUPERLU_FREE(tmp);
		} else {
		    for (j = 0; j < nrhs; ++j)
			for (i = 0; i < A->nrow; ++i) {
                           zd_mult(&Xmat[i+j*ldx], &Xmat[i+j*ldx], R[i]);
                        }
		}
	    }
	}
    } /* end if nrhs > 0 */

    if ( options->ConditionNumber ) {
	/* Set INFO = A->ncol+1 if the matrix is singular to working precision. */
	if ( *rcond < dlamch_("E") && *info == 0) *info = A->ncol + 1;
    }

    if (perm) SUPERLU_FREE(perm);

    if ( nofact ) {
	ilu_zQuerySpace(L, U, mem_usage);
	toast_Destroy_CompCol_Permuted(&AC);
    }
    if ( A->Stype == SLU_NR ) {
	toast_Destroy_SuperMatrix_Store(AA);
	SUPERLU_FREE(AA);
    }

}