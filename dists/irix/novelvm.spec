product novelvm
    id "NovelVM 2.3.0git"
    image sw
        id "software"
        version 18
        order 9999
        subsys eoe default
            id "execution only env"
            replaces self
            exp novelvm.sw.eoe
        endsubsys
    endimage
    image man
        id "man pages"
        version 18
        order 9999
        subsys readme default
            id "novelvm documentation"
            replaces self
            exp novelvm.man.readme
        endsubsys
    endimage
endproduct
