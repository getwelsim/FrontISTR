module hecmw_matrix_misc
  use hecmw_util
  use hecmw_matrix_contact
  use m_hecmw_comm_f

  contains

  subroutine hecmw_mat_clear( hecMAT )
    type(hecmwST_matrix) :: hecMAT

    hecMAT%D = 0.0d0
    hecMAT%AL = 0.0d0
    hecMAT%AU = 0.0d0
    call hecmw_cmat_clear( hecMAT%cmat )
    call hecmw_mat_set_penalized( hecMAT, 0 )
  end subroutine hecmw_mat_clear

  subroutine hecmw_mat_clear_b( hecMAT )
    type(hecmwST_matrix) :: hecMAT

    hecMAT%B = 0.0d0
    call hecmw_mat_set_penalized_b( hecMAT, 0 )
  end subroutine hecmw_mat_clear_b

  subroutine hecmw_mat_init( hecMAT )
    type(hecmwST_matrix) :: hecMAT

    hecMAT%Iarray = 0
    hecMAT%Rarray = 0.d0

    call hecmw_mat_set_iter( hecMAT, 100 )
    call hecmw_mat_set_nrest( hecMAT, 10 )

    call hecmw_mat_set_resid( hecMAT, 1.d-8 )
    call hecmw_mat_set_sigma_diag( hecMAT, 1.d0 )
    call hecmw_mat_set_sigma( hecMAT, 0.d0 )
    call hecmw_mat_set_thresh( hecMAT, 0.10d0 )
    call hecmw_mat_set_filter( hecMAT, 0.10d0 )

    call hecmw_mat_set_penalized( hecMAT, 0 )
    call hecmw_mat_set_penalty( hecMAT, 1.d+4 )

    call hecmw_mat_set_dump( hecMAT, 0 )
  end subroutine hecmw_mat_init

  subroutine hecmw_mat_set_iter( hecMAT, iter )
    type(hecmwST_matrix) :: hecMAT
    integer(kind=kint) :: iter

    hecMAT%Iarray(1) = iter
  end subroutine hecmw_mat_set_iter

  function hecmw_mat_get_iter( hecMAT )
    integer(kind=kint) :: hecmw_mat_get_iter
    type(hecmwST_matrix) :: hecMAT

    hecmw_mat_get_iter = hecMAT%Iarray(1)
  end function hecmw_mat_get_iter

  subroutine hecmw_mat_set_method( hecMAT, method )
    type(hecmwST_matrix) :: hecMAT
    integer(kind=kint) :: method

    hecMAT%Iarray(2) = method
  end subroutine hecmw_mat_set_method

  function hecmw_mat_get_method( hecMAT )
    integer(kind=kint) :: hecmw_mat_get_method
    type(hecmwST_matrix) :: hecMAT

    hecmw_mat_get_method = hecMAT%Iarray(2)
  end function hecmw_mat_get_method

  subroutine hecmw_mat_set_precond( hecMAT, precond )
    type(hecmwST_matrix) :: hecMAT
    integer(kind=kint) :: precond

    hecMAT%Iarray(3) = precond
  end subroutine hecmw_mat_set_precond

  function hecmw_mat_get_precond( hecMAT )
    integer(kind=kint) :: hecmw_mat_get_precond
    type(hecmwST_matrix) :: hecMAT

    hecmw_mat_get_precond = hecMAT%Iarray(3)
  end function hecmw_mat_get_precond

  subroutine hecmw_mat_set_nset( hecMAT, nset )
    type(hecmwST_matrix) :: hecMAT
    integer(kind=kint) :: nset

    hecMAT%Iarray(4) = nset
  end subroutine hecmw_mat_set_nset

  function hecmw_mat_get_nset( hecMAT )
    integer(kind=kint) :: hecmw_mat_get_nset
    type(hecmwST_matrix) :: hecMAT

    hecmw_mat_get_nset = hecMAT%Iarray(4)
  end function hecmw_mat_get_nset

  subroutine hecmw_mat_set_iterpremax( hecMAT, iterpremax )
    type(hecmwST_matrix) :: hecMAT
    integer(kind=kint) :: iterpremax

        if (iterpremax.lt.1) iterpremax= 1
        if (iterpremax.gt.4) iterpremax= 4

    hecMAT%Iarray(5) = iterpremax
  end subroutine hecmw_mat_set_iterpremax

  function hecmw_mat_get_iterPREmax( hecMAT )
    integer(kind=kint) :: hecmw_mat_get_iterPREmax
    type(hecmwST_matrix) :: hecMAT

    hecmw_mat_get_iterPREmax = hecMAT%Iarray(5)
  end function hecmw_mat_get_iterPREmax

  subroutine hecmw_mat_set_nrest( hecMAT, nrest )
    type(hecmwST_matrix) :: hecMAT
    integer(kind=kint) :: nrest

    hecMAT%Iarray(6) = nrest
  end subroutine hecmw_mat_set_nrest

  function hecmw_mat_get_nrest( hecMAT )
    integer(kind=kint) :: hecmw_mat_get_nrest
    type(hecmwST_matrix) :: hecMAT

    hecmw_mat_get_nrest = hecMAT%Iarray(6)
  end function hecmw_mat_get_nrest

  subroutine hecmw_mat_set_penalized( hecMAT, penalized )
    type(hecmwST_matrix) :: hecMAT
    integer(kind=kint) :: penalized

    hecMAT%Iarray(11) = penalized
  end subroutine hecmw_mat_set_penalized

  function hecmw_mat_get_penalized( hecMAT )
    integer(kind=kint) :: hecmw_mat_get_penalized
    type(hecmwST_matrix) :: hecMAT

    hecmw_mat_get_penalized = hecMAT%Iarray(11)
  end function hecmw_mat_get_penalized

  subroutine hecmw_mat_set_penalized_b( hecMAT, penalized_b )
    type(hecmwST_matrix) :: hecMAT
    integer(kind=kint) :: penalized_b

    hecMAT%Iarray(12) = penalized_b
  end subroutine hecmw_mat_set_penalized_b

  function hecmw_mat_get_penalized_b( hecMAT )
    integer(kind=kint) :: hecmw_mat_get_penalized_b
    type(hecmwST_matrix) :: hecMAT

    hecmw_mat_get_penalized_b = hecMAT%Iarray(12)
  end function hecmw_mat_get_penalized_b

  subroutine hecmw_mat_set_iterlog( hecMAT, iterlog )
    type(hecmwST_matrix) :: hecMAT
    integer(kind=kint) :: iterlog

    hecMAT%Iarray(21) = iterlog
  end subroutine hecmw_mat_set_iterlog

  function hecmw_mat_get_iterlog( hecMAT )
    integer(kind=kint) :: hecmw_mat_get_iterlog
    type(hecmwST_matrix) :: hecMAT

    hecmw_mat_get_iterlog = hecMAT%Iarray(21)
  end function hecmw_mat_get_iterlog

  subroutine hecmw_mat_set_timelog( hecMAT, timelog )
    type(hecmwST_matrix) :: hecMAT
    integer(kind=kint) :: timelog

    hecMAT%Iarray(22) = timelog
  end subroutine hecmw_mat_set_timelog

  function hecmw_mat_get_timelog( hecMAT )
    integer(kind=kint) :: hecmw_mat_get_timelog
    type(hecmwST_matrix) :: hecMAT

    hecmw_mat_get_timelog = hecMAT%Iarray(22)
  end function hecmw_mat_get_timelog

  subroutine hecmw_mat_set_resid( hecMAT, resid )
    type(hecmwST_matrix) :: hecMAT
    real(kind=kreal) :: resid

    hecMAT%Rarray(1) = resid
  end subroutine hecmw_mat_set_resid

  function hecmw_mat_get_dump( hecMAT )
    integer(kind=kint) :: hecmw_mat_get_dump
    type(hecmwST_matrix) :: hecMAT
    hecmw_mat_get_dump = hecMAT%Iarray(31)
  end function hecmw_mat_get_dump

  subroutine hecmw_mat_set_dump( hecMAT, dump_type )
    type(hecmwST_matrix) :: hecMAT
    integer(kind=kint) :: dump_type
    hecMAT%Iarray(31) = dump_type
  end subroutine hecmw_mat_set_dump

  function hecmw_mat_get_resid( hecMAT )
    real(kind=kreal) :: hecmw_mat_get_resid
    type(hecmwST_matrix) :: hecMAT

    hecmw_mat_get_resid = hecMAT%Rarray(1)
  end function hecmw_mat_get_resid

  subroutine hecmw_mat_set_sigma_diag( hecMAT, sigma_diag )
    type(hecmwST_matrix) :: hecMAT
    real(kind=kreal) :: sigma_diag

    hecMAT%Rarray(2) = sigma_diag
  end subroutine hecmw_mat_set_sigma_diag

  function hecmw_mat_get_sigma_diag( hecMAT )
    real(kind=kreal) :: hecmw_mat_get_sigma_diag
    type(hecmwST_matrix) :: hecMAT

    hecmw_mat_get_sigma_diag = hecMAT%Rarray(2)
  end function hecmw_mat_get_sigma_diag

  subroutine hecmw_mat_set_sigma( hecMAT, sigma )
    type(hecmwST_matrix) :: hecMAT
    real(kind=kreal) :: sigma

    hecMAT%Rarray(3) = sigma
  end subroutine hecmw_mat_set_sigma

  function hecmw_mat_get_sigma( hecMAT )
    real(kind=kreal) :: hecmw_mat_get_sigma
    type(hecmwST_matrix) :: hecMAT

    hecmw_mat_get_sigma = hecMAT%Rarray(3)
  end function hecmw_mat_get_sigma

  subroutine hecmw_mat_set_thresh( hecMAT, thresh )
    type(hecmwST_matrix) :: hecMAT
    real(kind=kreal) :: thresh

    hecMAT%Rarray(4) = thresh
  end subroutine hecmw_mat_set_thresh

  function hecmw_mat_get_thresh( hecMAT )
    real(kind=kreal) :: hecmw_mat_get_thresh
    type(hecmwST_matrix) :: hecMAT

    hecmw_mat_get_thresh = hecMAT%Rarray(4)
  end function hecmw_mat_get_thresh

  subroutine hecmw_mat_set_filter( hecMAT, filter )
    type(hecmwST_matrix) :: hecMAT
    real(kind=kreal) :: filter

    hecMAT%Rarray(5) = filter
  end subroutine hecmw_mat_set_filter

  function hecmw_mat_get_filter( hecMAT )
    real(kind=kreal) :: hecmw_mat_get_filter
    type(hecmwST_matrix) :: hecMAT

    hecmw_mat_get_filter = hecMAT%Rarray(5)
  end function hecmw_mat_get_filter

  subroutine hecmw_mat_set_penalty( hecMAT, penalty )
    type(hecmwST_matrix) :: hecMAT
    real(kind=kreal) :: penalty

    hecMAT%Rarray(11) = penalty
  end subroutine hecmw_mat_set_penalty

  function hecmw_mat_get_penalty( hecMAT )
    real(kind=kreal) :: hecmw_mat_get_penalty
    type(hecmwST_matrix) :: hecMAT

    hecmw_mat_get_penalty = hecMAT%Rarray(11)
  end function hecmw_mat_get_penalty

  function hecmw_mat_diag_max(hecMAT, hecMESH)
    real(kind=kreal) :: hecmw_mat_diag_max
    type (hecmwST_matrix) :: hecMAT
    type (hecmwST_local_mesh) :: hecMESH
     integer(kind=kint) :: ndiag, i

    hecmw_mat_diag_max = -1.0e20
    ndiag = hecMAT%NDOF**2 * hecMAT%NP
    do i = 1, ndiag
      if( hecMAT%D(i) > hecmw_mat_diag_max ) hecmw_mat_diag_max = hecMAT%D(i)
    enddo
    call hecmw_allREDUCE_R1(hecMESH, hecmw_mat_diag_max, hecmw_max)
  end function hecmw_mat_diag_max

end module hecmw_matrix_misc
