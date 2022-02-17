#!/bin/sh

git rebase scummvm/master
find engines -type d -maxdepth 1 -not -name '.deps' -not -name 'smt' -not -name 'bibleblack' -not -name 'koihime_musou'
