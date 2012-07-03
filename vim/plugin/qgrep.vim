if ( exists('g:loaded_qgrep') && g:loaded_qgrep ) || v:version < 700 || &cp
	finish
endif

let g:loaded_qgrep = 1

" Options
let g:Qgrep = {
    \ 'qgrep': 'libcall:'.expand('<sfile>:h:h').'/qgrep',
    \ 'project': '*',
    \ 'searchtype': 'ft',
    \ 'limit': 100,
    \ 'keymap': {
        \ 'qgrep#selectProject()':  ['<C-q>'],
    \ },
    \ 'lazyupdate': 0,
    \ 'maxheight': 7,
    \ }

" Commands
command! -n=? Qgrep call qgrep#open()
command! -n=? QgrepProject call qgrep#selectProject()

" Mappings
silent! nnoremap <silent> <C-p> :Qgrep<CR>
