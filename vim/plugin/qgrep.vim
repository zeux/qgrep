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
        \ 'qgrep#selectProject()':  ['<C-q>'],
        \ 'qgrep#acceptSelection()': ['<CR>'],
        \ },
    \ 'searchtype': 'ft',
    \ 'mode': 'files',
    \ 'search': {
        \ 'lazyupdate': 1
        \ },
    \ }

" Commands
command! -n=? Qgrep call qgrep#open(<f-args>)
command! -n=? QgrepProject call qgrep#selectProject(<f-args>)

" Mappings
silent! nnoremap <silent> <C-p> :Qgrep<CR>
