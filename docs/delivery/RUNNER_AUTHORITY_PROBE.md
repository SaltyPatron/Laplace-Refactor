# Runner-authority path probe

This repair isolates the DEV/BAT runner's view of host prerequisites without changing any activation acceptance condition.

The probe distinguishes a genuinely absent path (`ENOENT`) from an inaccessible path (`EACCES`) and reports each path component up to the first failure. It exists because the previous inline diagnostic converted any failed `stat(2)` into the string `<missing>`, which could misclassify traversal or permission failures as absence.

The temporary `runner-authority-probe` workflow runs only for the repair branch and can be removed after the runner/host discrepancy is resolved. It does not compose, activate, modify, delete, or migrate product state.
