/* For the odds and ends we need to #define */ 

/* termios.h : c_line */
#undef HAVE_TERMIOS_C_LINE

/* netinet/in.h : sin_len */
#undef HAVE_SOCKADDR_IN_SIN_LEN

/* limits.h: CLK_TCK */
#undef CLK_TCK_IN_LIMITS_H

/* time.h: CLK_TCK */
#undef CLK_TCK_IN_TIME_H
