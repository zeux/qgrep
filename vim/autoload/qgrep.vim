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

function! s:renderStatus(matches)
    let str = '%#LineNr#qgrep%* '

    if a:matches < 128
        let str .= printf("%d matches", a:matches)
    else
        let str .= printf("%d+ matches", a:matches)
    endif

    let &l:statusline = str
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
        let res .= "\b"
        let res .= strpart(a:text, pos, 1)
        let i += 1
        let last = pos
    endwhile
    let res .= strpart(a:text, last + 1)
    return res
endfunction

function! s:updateResults(pattern)
    let qgrep = expand('$VIM') . '\ext\qgrep.dll'
    let qgrep_args = printf("files\nea\nft\nL%d\nft\n%s", 128, a:pattern)
    let results = split(libcall(qgrep, 'entryPointVim', qgrep_args), "\n")
    call map(results, 's:hixform(v:val, a:pattern)')
    call s:renderResults(results)
    call s:renderStatus(len(results))
endfunction

function! s:prompt(input, onkey)
    let str = ""
    let cur = 0

    while 1
        call s:updateResults(str)
        call s:renderPrompt(a:input, str, cur)

        " get input
        let ch = getchar()

        if type(ch) == type(0) && ch >= 32
            " regular character
            let str = strpart(str, 0, cur) . nr2char(ch) . strpart(str, cur)
            let cur += 1
        else
            " special character
            let ch = (type(ch) == type(0)) ? nr2char(ch) : ch

            if ch == "\<Esc>"
                break
            elseif ch == "\<Left>"
                if cur > 0
                    let cur -= 1
                endif
            elseif ch == "\<Right>"
                if cur < len(str)
                    let cur += 1
                endif
            elseif ch == "\<BS>"
                if cur > 0
                    let str = strpart(str, 0, cur - 1) . strpart(str, cur)
                    let cur -= 1
                endif
            elseif ch == "\<Del>"
                if cur < len(str)
                    let str = strpart(str, 0, cur) . strpart(str, cur + 1)
                endif
            elseif a:onkey(ch, str) == 1
                break
            endif
        endif
    endwhile

    return str
endfunction

function! s:onkeydummy(ch, str)
    if a:ch == "\<Up>"
        keepjumps normal! k
    elseif a:ch == "\<Down>"
        keepjumps normal! j
    endif
endfunction

function! s:open()
	silent! keepalt botright 1new Qgrep
    syntax clear
    syntax match Identifier /\b\@<=./
    syntax match Ignore /\b/ conceal
    setlocal conceallevel=3
    setlocal concealcursor=nvic
    setlocal cursorline

    setlocal number
    setlocal noswapfile
    setlocal nobuflisted
    setlocal nowrap
    setlocal nolist
    setlocal nospell
    setlocal nocursorcolumn
    setlocal winfixheight
    setlocal nofoldenable
    setlocal textwidth=0
    setlocal buftype=nofile
    setlocal bufhidden=unload

	if v:version > 702
        setlocal norelativenumber
        setlocal noundofile
        setlocal colorcolumn=0
	endif
endfunction

function! s:close()
    bunload!
endfunction

function! qgrep#prompt(input)
    noautocmd call s:open()
    call s:prompt(a:input, function("<SID>onkeydummy"))
    noautocmd call s:close()
endfunction

call qgrep#prompt(">>>")
