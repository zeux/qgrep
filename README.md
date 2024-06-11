qgrep [![Actions Status](https://github.com/zeux/qgrep/workflows/build/badge.svg)](https://github.com/zeux/qgrep/actions)
=====

qgrep is an implementation of grep database, which allows you to perform
grepping (i.e. full-text searches using regular expressions) over a large set
of files. Searches use the database which is a compressed and indexed copy
of the source data, thus they are much faster compared to vanilla grep -R.

Basic setup
-----------

The easiest way to start using qgrep is to use the init command:

    qgrep init <project> <project-path>

i.e.

    qgrep init mygame D:\MyGame\Source

It will create the project configuration file ~/.qgrep/mygame.cfg that will
index all source files (from a certain predefined set of extensions) in Source
folder, including subfolders. After that you have to update the database:

    qgrep update mygame

And start using it:

    qgrep search mygame main\s*\(

Note that you'll have to update the database from time to time in order to
keep the search results current; you can run `qgrep update mygame` as a scheduled
task or manually.

Projects
--------

Qgrep stores one database for each project, where project is a collection of
text files. Projects are set up using configuration files, which normally live
in ~/.qgrep folder (you can store projects in other folders, but you'll have to
specify the full project path for all commands instead of project name).

Note: ~ on Windows means the home directory as set by HOME or HOMEPATH
environment variables (usually it's the profile directory, C:\Users\UserName)

Each project consists of the configuration file with .cfg extension (this is a
text file that specifies the set of files to be put into the database), and
files with other extensions (i.e. .qgd, .qgf), that contain the database itself.

Projects have short names that are essentially relative paths from .qgrep folder
without the extension - i.e. project 'foo' corresponds to project configuration
file ~/.qgrep/foo.cfg. Project names can be hierarchical - i.e. foo/bar.

Project list
------------

Most commands (except init) accept a project list. It is a comma-separated list
of items, where each item can be one of:

    *     - all projects in ~/.qgrep, including subprojects (hierarchical names)
    name  - project with a specified name, i.e. mygame
    name/ - all subprojects of the project name, i.e. foo/ includes foo/bar (but
            does not include foo)
    path  - full path to a project .cfg file for projects outside ~/.qgrep

For example:

    mygame,mygame/art - include ~/.qgrep/mygame.cfg and ~/.qgrep/mygame/art.cfg
    *,D:\mygame\source.cfg - all projects in ~/.qgrep and D:\mygame\source.cfg

Project configuration file format
---------------------------------

Project configuration files are line-based text files, which specify a nested
set of groups, where each group can have a set of file paths, a set of folder
paths that are scanned hierarchically, and a set of include/exclude regular
expressions, that are used to filter contents of path scanning. Here is a
complete example with all available syntax:

    include \.(cpp|c|hpp|h)$
    # this is a comment

    group
        path D:\MyGame\Thirdparty
        include \.(py|pl)$
        exclude ^boost/
    endgroup

    group
        path D:\MyGame\Sources
        include \.hlsl$
    endgroup

    file D:\MyGame\designdoc.txt

    # note how you can omit 'file'
    D:\MyGame\technicaldesigndoc.txt

In this example there are two groups in root group; one contains all files from
Thirdparty folder that have one of cpp, c, hpp, h, py, pl extensions (note that
for the file to be included, it has to match one of the include patterns
specified in the current group or one of its ancestors) with the exception of
the entire boost/ folder; the second group contains all files from Sources
folder that have one of cpp, c, hpp, h, hlsl extensions. Also the root group
contains two more files, designdoc.txt and technicaldesigndoc.txt.

Since you can omit 'file' prefix for single file names, a file list works as a
valid project configuration file.

Updating the project
--------------------

Updating the project is done with

    qgrep update <project-list>

This updates the project by reading the project configuration file for all
specified projects, converting it to file list, then reads all files
from disk and puts them to the database. For large projects, both reading the
file list and reading the file contents takes a bit of time, so be patient.

Update tries to reuse the information from the existing database (if available)
to speed up the process. The implementation relies on file metadata, and will
incorrectly preserve the old contents if the file contents changed without
changing the modification time or file size (however, this is extremely rare,
so is probably not a big concern). You can use `qgrep build` instead of `update`
to force a clean build.

Remember that you can use * as a shorthand for all projects: `qgrep update *'
updates everything.

Searching the project
---------------------

The command for searching the project is:

    qgrep search <project-list> <search-options> <query>

Query is a regular expression by default; you can use search options to change
it to literal. Remember that query is the last argument - you will need to quote
it if your query needs to contain a space.

Search options do not have a specific prefix, and can be separated by spaces.
These are the available search options:

    i - case-insensitive search
    l - literal search (query is treated as a literal string)
    b - bruteforce search: skip indexing optimizations (mainly for internal use)
    V - Visual Studio style formatting: slashes are replaced with backslashes
        and line number is printed in parentheses
    C - include column number in output
    CE - include starting and ending column numbers in output
    Lnumber - limit output to <number> lines

For example, this command uses case-insensitive regex search with Visual Studio
output formats (with column number included), limited to 100 results:

    qgrep search * i VC L100 hello\s+world

Searching for project files
---------------------------

Since the database contains file list in addition to file contents, qgrep can be
used to search for files by paths or names. The command for that is:

    qgrep files <project-list> <search-options> <query>

You can omit search options and query to get all files in the project(s).

Search options can contain all options that are used for regular searches
(although not all options make sense for file searches); in addition, you can
select a search style using the following options

    fp - search in file paths using a regular expression (unless l flag is used)
         This option is the default.

    fn - search in file names using a regular expression (unless l flag is used)

    fs - search in file names/paths using a space-delimited literal query
         The query is a space-delimited list of literal components; if a component
         contains a slash, it is used to filter files by path; otherwise it is used
         to filter files by name. For example: 

            render/ manager.c

         matches with:

            D:\MyGame\Source/render/lightmanager.cpp
            D:\MyGame\Source/render/manager.c

    ff - search in file paths using fuzzy matching with ranking
         All letters from a query have to exist in the file path in the same order,
         and the distance between letters in the match determines the score. For
         example:

            src/r/lmanager

         matches with:

            D:\MyGame\Source/render/lightmanager.cpp
            D:\MyGame\Source/network/lobby/manager.cpp

Keeping projects up-to-date
---------------------------

While you can use `qgrep update` to keep projects up to date, this is not very
convenient.  If you forget to do that the searches will return stale data, and
you'd have to update after every significant change.

Because of this, qgrep provides functionality that lets you notify it about any
changes (you can set it to run after your editor saves the file):

	qgrep change <project-list> <file-list>

This will update the list of changed files for each project to include the files
that are part of that project - you can specify * as a shorthand for all
projects.

Additionally, you can run qgrep in watch mode where it will automatically listen
to any filesystem changes and update the changed files:

	qgrep watch <project-list>

Internally qgrep keeps a list of changed files for each project, and `change`
simply appends the specified files to the list. Because of this, if you only use
`change` and never `update`, over time the search performance will deteriorate;
`watch`, however, will automatically update the project when the list grows
large enough to maintain query performance.

Note that currently `change`/`watch` do not track new files, only changes to
existing files.

Feedback
--------

You can report bugs, feature requests, submit patches and download new versions
from qgrep site:

    http://github.com/zeux/qgrep

Alternatively, you can contact the author using the e-mail:

    Arseny Kapoulkine <arseny.kapoulkine@gmail.com>

License
-------

This software is available to anybody free of charge, under the terms of MIT License (see LICENSE.md).
