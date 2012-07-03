function! s:echoHighlight(group, text)
    execute 'echohl' a:group
    echon a:text
    echohl None
endfunction

function! s:renderPrompt(prompt, text, cursor)
    redraw
    call s:echoHighlight('Comment', a:prompt)
    call s:echoHighlight('Normal', strpart(a:text, 0, a:cursor))
    call s:echoHighlight('Constant', strpart(a:text, a:cursor, 1))
    call s:echoHighlight('Normal', strpart(a:text, a:cursor + 1))

    if a:cursor >= len(a:text)
        " call s:echoHighlight('Constant', '_')
    endif
endfunction

function! s:renderResults(lines)
    let height = min([len(a:lines), 5])
    execute '%d'
    execute 'resize' height
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
endfunction

function! s:onMoveLine(state, type)
    let state = a:state
    execute 'keepjumps' 'normal!' a:type
    let state.line = line('.')

    " make sure there are no empty lines on screen
    let empty = winheight(0) - (line('w$') - line('w0') + 1)

    if empty > 0
        execute 'keepjumps' 'normal!' (empty . "\<c-y>")
    endif
endfunction

function! s:prompt(input)
    let state = {}
    let state.cursor = 0
    let state.pattern = ''
    let state.line = 0

    let keymap = {
        \ 'onDeleteChar(-1)':   ['<bs>', '<c-]>'],
        \ 'onDeleteChar(0)':    ['<del>'],
        \ 'onMoveLine("j")':    ['<c-j>', '<down>'],
        \ 'onMoveLine("k")':    ['<c-k>', '<up>'],
        \ 'onMoveLine("gg")':   ['<Home>', '<kHome>'],
        \ 'onMoveLine("G")':    ['<End>', '<kEnd>'],
        \ 'onMoveLine("\<c-b>")':    ['<PageUp>', '<kPageUp>'],
        \ 'onMoveLine("\<c-f>")':    ['<PageDown>', '<kPageDown>'],
        \ 'onMoveCursor(-1)':   ['<c-h>', '<left>', '<c-^>'],
        \ 'onMoveCursor(+1)':   ['<c-l>', '<right>'],
        \ }

    call s:updateResults(state)

    while 1
        call s:renderPrompt(a:input, state.pattern, state.cursor)

        " get input
        let ch = getchar()

        if type(ch) == type(0) && ch >= 32
            call s:onInsertChar(state, nr2char(ch))
        else
            let ch = (type(ch) == type(0)) ? nr2char(ch) : ch

            if ch ==# "\<Esc>"
                break
            endif

            for [k,v] in items(keymap)
                for kp in v
                    if ch ==# eval('"\' . kp . '"')
                        let pos = stridx(k, '(')
                        let expr = '<SID>' . strpart(k, 0, pos + 1) . 'state, ' . strpart(k, pos + 1)
                        call eval(expr)
                    endif
                endfor
            endfor
        endif
    endwhile
endfunction

function! s:open()
	silent! keepalt botright 1new Qgrep
    syntax clear
    syntax match Identifier /\%x16\@<=./
    syntax match Ignore /\%x16/ conceal

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

function! s:close()
    bunload!
endfunction

function! qgrep#prompt(input)
    noautocmd call s:open()
    call s:prompt(a:input)
    noautocmd call s:close()
endfunction

call qgrep#prompt(">>>")
