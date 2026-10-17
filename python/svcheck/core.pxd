from svcheck cimport core_decl

cdef class Factory(object):
    cdef core_decl.IFactory *_hndl

cdef class Compilation(object):
    cdef core_decl.ICompilation *_hndl
