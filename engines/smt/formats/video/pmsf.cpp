#include "smt/formats/video/pmsf.h"


void PMSFFile::ReadFile(char *path)
{
    Common::File f;
    if (f.open(path))
    {
        //header
        f.read(&dat.header, sizeof(PSMFHeader));
        
        //stream info size
        f.read(&dat.stream_info, sizeof(PSMFStreamInfo));
        
        //streams
        //dat.streams = Common::Array<PSMFStream>(dat.stream_info.num_streams2);
        for (int i = 0; i < dat.stream_info.num_streams2; i++)
        {
            PSMFStream _stream;
            f.read(&_stream, sizeof(PSMFStream));
            dat.streams.insert_at(i, _stream);
        }
        byte numEP = 0;
        while (f.eos())
        {
            numEP++;
            PSMFEPEntry _entry;
            f.read(&dat.ep_entries[numEP], sizeof(sizeof(PSMFEPEntry)));
            dat.ep_entries.insert_at(numEP, _entry);

        }
                

    }
}

PMSFFile::PMSFFile()
{
}
