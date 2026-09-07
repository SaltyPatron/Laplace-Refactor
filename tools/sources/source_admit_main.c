#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Only locate and execute the packaged transport. All Laplace operations are
 * dispatched by that transport to the same installed native engine as SQL. */
int main(int argc, char **argv) {
    char executable[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1u);
    if (length < 0 || (size_t)length >= sizeof(executable) - 1u) {
        fputs("laplace-source-admit: cannot locate installed executable\n", stderr);
        return 1;
    }
    executable[length] = '\0';
    char *separator = strrchr(executable, '/');
    if (separator == NULL) return 1;
    *separator = '\0';
    char runtime[PATH_MAX], assembly[PATH_MAX];
    int runtime_length = snprintf(runtime, sizeof(runtime),
        "%s/../share/laplace/source-admission/runtime/dotnet", executable);
    int assembly_length = snprintf(assembly, sizeof(assembly),
        "%s/../share/laplace/source-admission/laplace-source-admit.dll", executable);
    if (runtime_length < 0 || (size_t)runtime_length >= sizeof(runtime) ||
        assembly_length < 0 || (size_t)assembly_length >= sizeof(assembly)) {
        fputs("laplace-source-admit: installed path exceeds its bound\n", stderr);
        return 1;
    }
    char **arguments = calloc((size_t)argc + 2u, sizeof(*arguments));
    if (arguments == NULL) return 1;
    arguments[0] = runtime;
    arguments[1] = assembly;
    for (int index = 1; index < argc; ++index) arguments[index + 1] = argv[index];
    execv(runtime, arguments);
    int error = errno;
    free(arguments);
    fprintf(stderr, "laplace-source-admit: cannot execute packaged runtime: %s\n", strerror(error));
    return 1;
}
