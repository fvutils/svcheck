// SL-2: a rand property the class inherits is a legal randomize() argument
// (IEEE 1800-2017 18.11). From OpenTitan's uart_fifo_reset_vseq, whose
// DV_CHECK_MEMBER_RANDOMIZE_FATAL(dly_to_access_fifo) names a base's property.
class base;
  rand int unsigned d;
endclass

class derived extends base;
  rand int unsigned e;
  function bit f();
    return this.randomize(d) && randomize(d, e);
  endfunction
endclass
