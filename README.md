# CTF solves

`for f in $(ls -A); do git ls-files | grep -F "$f" >/dev/null 2>&1 || rm -rf $f ; done`
