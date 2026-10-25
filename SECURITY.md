# Security

Hawser runs as root and configures your network, so a flaw in it matters.

## Reporting a vulnerability

Please do not open a public issue for a security problem. Report it privately
through the project's vulnerability reporting form on GitHub (the Security tab
of the repository), or by email to dev.chempa@gmail.com. Say what the problem
is, how to reproduce it, and which version you saw it in (`hawser version`).

You will get a reply within a week. Once a fix is released the report is
credited to you in the release notes, unless you ask otherwise.

## What is and is not in scope

In scope: anything that lets a machine outside the network read, send or
disturb traffic inside it; lets a machine inside the network act as another;
lets an invite be used more than once or by someone other than its recipient;
or makes hawser write outside the files listed in the README.

Out of scope: WireGuard itself (report to the WireGuard project), and the
limits the README lists openly, such as every machine in the network being
able to reach every other.
