#
# Makefile for ldirectord
#
# Produces all manner of documentation for ldirectord
#

POD2MAN=pod2man

.PHONY=clean all

all: ldirectord.8

clean:
	$(RM) -f ldirectord.8

ldirectord.8:	ldirectord
	$(POD2MAN) $< > $@
