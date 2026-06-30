.PHONY: all test clean format format-check tidy cppcheck sanitize coverage bench

all test clean format format-check tidy cppcheck sanitize coverage bench:
	$(MAKE) -C mosrt $@
