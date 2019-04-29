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
    \ 'searchtype': 'ff',
    \ 'mode': 'files',
    \ 'search': {
        \ 'lazyupdate': 1,
        \ 'maxheight': 15,
        \ 'keymap': {
            \ 'qgrep#search#toggleIgnoreCase(%s)': ['<C-i>'],
            \ 'qgrep#search#toggleLiteral(%s)': ['<C-l>'],
            \ },
        \ },
    \ }

" Commands
command! -n=* Qgrep call qgrep#open(<f-args>)
command! -n=* QgrepSelectProject call qgrep#selectProject(<f-args>)
command! -n=* QgrepRun execute '!'.fnamemodify(g:qgrep.qgrep, ':s/^libcall://:r') join(map([<f-args>], 'shellescape(v:val)'))
command! -n=* QgrepUpdate if len(<q-args>) | execute 'QgrepRun' 'update' <args> | else | execute 'QgrepRun' 'update' g:qgrep.project | endif

" Commands for bundled extensions
command! -n=* QgrepBuffers call qgrep#open('buffers', <f-args>)
command! -n=* QgrepFiles call qgrep#open('files', <f-args>)
command! -n=* QgrepGlob call qgrep#open('glob', <f-args>)
command! -n=* QgrepProjects call qgrep#open('projects', <f-args>)
command! -n=* QgrepSearch call qgrep#open('search', <f-args>)

" Mappings
if !exists('g:qgrep_map') || g:qgrep_map == 1
    silent! nnoremap <silent> <C-p> :Qgrep<CR>
    silent! nnoremap <silent> <C-s> :QgrepSearch<CR>
endif
