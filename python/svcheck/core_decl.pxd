# libsvcheck's factory (src/api/include/svcheck/IFactory.h; svcheck-plan
# P2.6b), for svcheck.core.
from libc.stdint cimport int32_t, uint32_t
from libcpp.string cimport string as std_string
from libcpp.vector cimport vector as std_vector
from svcheck cimport sem_decl

cdef extern from "svcheck/IFactory.h" namespace "svcheck::api":
    cdef cppclass ICompilation:
        sem_decl.IDesign *getDesign()
        uint32_t getErrors()
        const std_string &getDiagnostics()
        std_string getFilePath(int32_t fileid)
        const std_vector[std_string] &getFiles()

    cdef cppclass IFactory:
        const char *getVersion()
        int runCli(const std_vector[std_string] &args, std_string &out, std_string &err)
        ICompilation *compile(const std_vector[std_string] &args, std_string &err)

ctypedef IFactory *IFactoryP
