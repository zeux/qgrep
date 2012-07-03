function! s:state()
    return s:state
endfunction

function! s:echoHighlight(group, text)
    execute 'echohl' a:group
    echon a:text
    echohl None
endfunction

function! s:renderPrompt(state)
    let state = a:state
    let text = state.pattern
    let cursor = state.cursor

    redraw
    call s:echoHighlight('Comment', '>>>')
    call s:echoHighlight('Normal', strpart(text, 0, cursor))
    call s:echoHighlight('Constant', strpart(text, cursor, 1))
    call s:echoHighlight('Normal', strpart(text, cursor + 1))

    if cursor >= len(text)
        call s:echoHighlight('Constant', '_')
    endif
endfunction

function! s:renderResults(lines)
    let height = min([len(a:lines), 5])
    silent! execute '%d'
    silent! execute 'resize' height
    call setline(1, a:lines)
endfunction

function! s:renderStatus(matches, uptime, retime)
    let res = []

    call add(res, "qgrep")

    if a:matches < 128
        call add(res, printf("%d matches", a:matches))
    else
        call add(res, printf("%d+ matches", a:matches))
    endif

    call add(res, printf("update %.f ms", a:uptime))
    call add(res, printf("render %.f ms", a:retime))

    let groups = ["LineNr", "None"]

    let &l:statusline = join(map(copy(res), '"%#" . groups[v:key % 2] . "# " . v:val . " %*"'), '')
endfunction

function! s:hixform(text, pattern)
    let ltext = tolower(a:text)
    let lpattern = tolower(a:pattern)
    let i = 0
    let last = -1
    let res = ''
    while i < len(a:pattern)
        let pos = stridx(ltext, strpart(lpattern, i, 1), last == -1 ? last : last + 1)
        let res .= strpart(a:text, last + 1, pos - last - 1)
        let res .= "\x16"
        let res .= strpart(a:text, pos, 1)
        let i += 1
        let last = pos
    endwhile
    let res .= strpart(a:text, last + 1)
    return res
endfunction

function! s:diffms(start, end)
    return str2float(reltimestr(reltime(a:start, a:end))) * 1000
endfunction

function! s:updateResults(state)
    let pattern = a:state.pattern
    let start = reltime()
    let qgrep = expand('$VIM') . '\ext\qgrep.dll'
    let qgrep_args = printf("files\nea\nft\nL%d\nft\n%s", 128, pattern)
    let results = split(libcall(qgrep, 'entryPointVim', qgrep_args), "\n")
    let mid = reltime()
    call map(results, 's:hixform(v:val, pattern)')
    call s:renderResults(results)
    call cursor(a:state.line, 1)
    let end = reltime()
    call s:renderStatus(len(results), s:diffms(start, mid), s:diffms(mid, end))

    " modifiable???
endfunction

function! s:onPatternChanged(state)
    call s:updateResults(a:state)
    call s:renderPrompt(a:state)
endfunction

function! s:onPromptChanged(state)
    call s:renderPrompt(a:state)
endfunction

function! s:onInsertChar(state, char)
    let state = a:state
    let state.pattern = strpart(state.pattern, 0, state.cursor) . a:char . strpart(state.pattern, state.cursor)
    let state.cursor += 1
    call s:onPatternChanged(state)
endfunction

function! s:onDeleteChar(state, offset)
    let state = a:state
    let state.pattern = strpart(state.pattern, 0, state.cursor + a:offset) . strpart(state.pattern, state.cursor + a:offset + 1)
    if state.cursor > 0 && a:offset < 0
        let state.cursor -= 1
    endif
    call s:onPatternChanged(state)
endfunction

function! s:onMoveCursor(state, diff)
    let state = a:state
    let state.cursor += a:diff
    let state.cursor = max([0, min([state.cursor, len(state.pattern)])])
    call s:onPromptChanged(state)
endfunction

function! s:onMoveLine(state, type)
    let state = a:state
    let motion = (a:type[0] == 'p') ? winheight(0) . a:type[1:] : a:type
    execute 'keepjumps' 'normal!' motion
    let state.line = line('.')
    call s:onPromptChanged(state)
endfunction

function! s:initSyntax()
    syntax clear
    syntax match Identifier /\%x16\@<=./
    syntax match Ignore /\%x16/ conceal
endfunction

function! s:initOptions()
    setlocal bufhidden=unload
    setlocal nobuflisted
    setlocal buftype=nofile
    setlocal colorcolumn=0
    setlocal concealcursor=n
    setlocal conceallevel=2
    setlocal nocursorcolumn
    setlocal cursorline
    setlocal foldcolumn=0
    setlocal nofoldenable
    setlocal nolist
    setlocal number
    setlocal numberwidth=4
    setlocal norelativenumber
    setlocal nospell
    setlocal noswapfile
    setlocal winfixheight
    setlocal nowrap
endfunction

function! s:initKeys(stateexpr)
    let keymap = {
        \ 'onDeleteChar(%s, -1)':   ['<bs>', '<c-]>'],
        \ 'onDeleteChar(%s, 0)':    ['<del>'],
        \ 'onMoveLine(%s, "j")':    ['<c-j>', '<down>'],
        \ 'onMoveLine(%s, "k")':    ['<c-k>', '<up>'],
        \ 'onMoveLine(%s, "gg")':   ['<Home>', '<kHome>'],
        \ 'onMoveLine(%s, "G")':    ['<End>', '<kEnd>'],
        \ 'onMoveLine(%s, "pk")':   ['<PageUp>', '<kPageUp>'],
        \ 'onMoveLine(%s, "pj")':   ['<PageDown>', '<kPageDown>'],
        \ 'onMoveCursor(%s, -1)':   ['<c-h>', '<left>', '<c-^>'],
        \ 'onMoveCursor(%s, +1)':   ['<c-l>', '<right>'],
        \ }

	" normal keys
    let charcmd = 'nnoremap <buffer> <silent> <char-%d> :call <SID>onInsertChar(%s, "%s")<CR>'
	for ch in range(32, 126)
		execute printf(charcmd, ch, a:stateexpr, escape(nr2char(ch), '"|\'))
	endfor

    " special keys
    for [expr, keys] in items(keymap)
        for key in keys
            execute 'nnoremap <buffer> <silent>' key ':call <SID>' . printf(expr, a:stateexpr) . '<CR>'
        endfor
    endfor
endfunction

function! s:open()
    let state = {}
    let state.cursor = 0
    let state.pattern = ''
    let state.line = 0

    let s:state = state

	silent! keepalt botright 1new Qgrep
    abclear <buffer>

    call s:initOptions()
    call s:initSyntax()
    call s:initKeys('<SID>state()')

    call s:updateResults(state)
    call s:renderPrompt(state)
endfunction

function! s:close()
    if exists('s:state')
        bunload!
        echo
        unlet! s:state
    endif
endfunction

function! qgrep#open()
    noautocmd call s:open()
endfunction

function! qgrep#close()
    noautocmd call s:close()
endfunction

if has('autocmd')
	augroup QgrepAug
		autocmd!
		autocmd BufLeave Qgrep call qgrep#close()
	augroup END
endif
