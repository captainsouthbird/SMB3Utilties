# SMB3Utilties
NoDice level editor and MusConv utility supporting my specific disassemblies. If you are running Windows, and you have no specific reason to build from source, you probably just want to use a prebuilt release version.

Windows users: You MUST install the GTK+ 2.24 runtime, available here: https://sourceforge.net/projects/gtk-win/

THIS IS ONLY FOR THE SOUTHBIRD SMB3 DISASSEMBLY IN ITS STANDARD DIRECTORY STRUCTURE. It will not work for general ROM hacks, nor will it work if you deviate the directory structure too much. (Without editing the editor's source anyway.)

To use NoDice, you will need to modify the config.xml to point to both where your game resides and the NESASM assembler resides. You can use relative or absolute paths depending on whatever makes the most sense for your environment. NOTE if you are using relative paths that the editor internally changes to the directory you specify for your SMB3 project, so remember to form your paths relative to that location. (Or just use aboslute paths.)

The following lines in config.xml are what you are most interested in:

------

"game" should point to where your project lives. Remember that the editor will internally change to this directory!

game value="C:\Users\Whatever\Documents\smb3"

------

"filebase" represents the root assembler file (that is, the file to be passed to NESASM) without any 3 letter extension (i.e. "smb3" means to use "smb3.asm" and generate "smb3.nes")

filebase value="smb3"

------

"build" represents the path to the NESASM executable. 

build value="C:\Users\Whatever\Documents\NoDice\nesasm"

------

Windows specific notes:

... whether you intend to build NoDice from source or run the release version!!

I compiled the release version under Visual Studio 2017 Community Edition.

This was written using GTK+ 2.0, which tends to be quite a bit of depedency hell under Windows. I have made available a prepackaged set of GTK+ 2.0 development files that make it ""easier"" to get this up and running without having to build all of GTK+ 2.0. Download that here: http://sonicepoch.com/downloads/gtk+-dev_2.24.10-1_win32-convenience-package.zip

NOTE: This is NOT in ANY way to be considered an "official" distribution of GTK+ 2.0, but merely a "convenience package." It is not easily maintained. If you have your own existing GTK+ 2.0 development environment, that will almost certainly be preferred. If you are interested in doing GTK+ 2.0 development in general, I strongly recommend you follow official documentation to get setup and do not use this package.

By default the included Visual Studio 2017 project looks for this GTK+ 2.0 "convenience package" at one level "beneath" the "sln" file. If you wish to use the project's default search paths, then this is where it needs to go.