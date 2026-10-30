# Security policy

gb-cplusplus parses untrusted media files (ISO BMFF, GPMF, and JPEG) and downloads map tiles over
HTTPS. Parser crashes, out-of-bounds reads or writes, and unbounded allocations are security
issues.

## Supported versions

Only the latest development series (currently 0.1) receives fixes.

## Reporting a vulnerability

Report it privately through GitHub:
**Security → Report a vulnerability** on the repository page. Do not open a public issue.
Include the tool and version (`--version`), and the smallest input that reproduces the problem.
Do not attach private recordings. Describe or minimize the input instead. You should receive a
response within 14 days.
