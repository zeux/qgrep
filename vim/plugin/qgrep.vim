if ( exists('g:loaded_qgrep') && g:loaded_qgrep ) || v:version < 700 || &cp
	finish
endif

let g:loaded_qgrep = 1

" Options
let g:qgrep = {
    \ 'qgrep': 'libcall:'.expand('<sfile>:h:h').'/qgrep',
    \ 'project': '*',
    \ 'limit': 100,
    \ 'lazyupdate': 0,
    \ 'maxheight': 7,
    \ 'switchbuf': '',
    \ 'keymap': {
        \ 'qgrep#selectProjectGUI()':  ['<C-p>'],
        \ 'qgrep#acceptSelection()': ['<CR>'],
        \ },
    \ 'searchtype': 'ft',
    \ 'mode': 'files',
    \ 'search': {
        \ 'lazyupdate': 1
        \ },
    \ }

" Commands
command! -n=* Qgrep call qgrep#open(<f-args>)
command! -n=* QgrepSelectProject call qgrep#selectProject(<f-args>)
command! -n=* QgrepRun execute '!'.fnamemodify(g:qgrep.qgrep, ':s/^libcall://:r') <q-args>

" Commands for bundled extensions
command! -n=* QgrepBuffers call qgrep#open('buffers', <f-args>)
command! -n=* QgrepFiles call qgrep#open('files', <f-args>)
command! -n=* QgrepGlob call qgrep#open('glob', <f-args>)
command! -n=* QgrepProjects call qgrep#open('projects', <f-args>)
command! -n=* QgrepSearch call qgrep#open('search', <f-args>)

" Mappings
silent! nnoremap <silent> <C-p> :Qgrep<CR>
