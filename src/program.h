#ifndef SQ_PROGRAM_H
#define SQ_PROGRAM_H

#include "value.h"

struct sq_program {
	unsigned nglobals;
	sq_value *globals;
	struct sq_function *main;
};

struct sq_program *sq_program_compile(const char *stream);
void sq_program_run(struct sq_program *program, unsigned argc, const char **argv);
void sq_program_free(struct sq_program *program);

#endif /* !SQ_PROGRAM_H */
