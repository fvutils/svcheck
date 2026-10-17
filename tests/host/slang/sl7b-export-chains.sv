package a_pkg; typedef int a_t; endpackage
package b_pkg; import a_pkg::*; export *::*; endpackage
package c_pkg; import b_pkg::*; export b_pkg::*; endpackage
package x_pkg; import y_pkg::*; export y_pkg::*; endpackage
package y_pkg; import x_pkg::*; export x_pkg::*; endpackage
module tb; import c_pkg::*; import x_pkg::*; a_t v; int w = nope; endmodule
