// SL-12 (lrm-relaxations, svcheck-plan P2.4b), a relaxation: a byte of a string compared with
// a string (OpenTitan's csrng and entropy_src DV)
module tb;
  string s = "abc", t = "b";
  int idx;
  initial foreach (s[i]) if (s[i] == t || t != s[i]) idx = i;
endmodule
