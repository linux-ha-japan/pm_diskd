/* For the odds and ends we need to #define */ 

/* termios.h : c_line */
#undef HAVE_TERMIOS_C_LINE

/* netinet/in.h : sin_len */
#undef HAVE_SOCKADDR_IN_SIN_LEN

/* limits.h: CLK_TCK */
#undef CLK_TCK_IN_LIMITS_H

/* time.h: CLK_TCK */
#undef CLK_TCK_IN_TIME_H


/* Borrowed from Proftpd
 * Proftpd is Licenced under the terms of the GNU General Public Licence
 * and is available from http://www.proftpd.org/
 */

/* Define this if you have the setpassent function */
#undef HAVE_SETPASSENT

/* If you don't have setproctitle, PF_ARGV_TYPE needs to be set to either
 * PF_ARGV_NEW (replace argv[] arguments), PF_ARGV_WRITEABLE (overwrite
 * argv[]), PF_ARGV_PSTAT (use the pstat function), or PF_ARGV_PSSTRINGS
 * (use PS_STRINGS).
 * 
 * configure should, we hope <wink>, detect this for you.
 */
#undef PF_ARGV_TYPE

/* Define if your system has __progname */
#undef HAVE___PROGNAME

/* Define if your system has the setproctitle function */
#undef HAVE_SETPROCTITLE

/* End of code borrowed from proftpd */
