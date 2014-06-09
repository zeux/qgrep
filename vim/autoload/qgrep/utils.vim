function! qgrep#utils#syntax(...)
    let syn = has('syntax') && exists('g:syntax_on')

    if syn
        for i in a:000
            if i == 'Path'
                syntax region QgrepPath start="^" end="\ze[^/\\]*$" contains=QgrepMatch oneline

                highlight default link QgrepPath SpecialComment
            elseif i == 'Match' && has('conceal')
                syntax region QgrepMatch
                    \ matchgroup=QgrepMatchBeg start=/\%o33\[.\{-}m/
                    \ matchgroup=QgrepMatchEnd end=/\%o33\[0m/
                    \ oneline concealends

                highlight default link QgrepMatch Identifier
            endif
        endfor
    endif

    return syn
endfunction

function! qgrep#utils#splitex(input)
    let pos = stridx(a:input, ':')
    let pos = pos < 0 ? len(a:input) : pos
    return [strpart(a:input, 0, pos), strpart(a:input, pos)]
endfunction

function! s:jumpCmd(cmd)
    if a:cmd != ''
        silent! execute a:cmd
        silent! normal! zvzz
    endif
endfunction

function! s:tabpagebufwinnr(idx)
    for i in range(tabpagenr('$'))
        let win = index(tabpagebuflist(i + 1), a:idx)
        if win >= 0
            return [i, win]
        endif
    endfor

    return [-1, -1]
endfunction

function! s:gotoFile(path, mode, cmd)
    let path = fnamemodify(a:path, ':p')
    let mode = split(a:mode, ',')

    let buf = bufnr('^'.path.'$')

    if (count(mode, 'useopen') || count(mode, 'usetab')) && buf >= 0
        let win = bufwinnr(buf)
        if win >= 0
            execute win . 'wincmd w'
            return s:jumpCmd(a:cmd)
        endif

        if count(mode, 'usetab')
            let [tab, win] = s:tabpagebufwinnr(buf)
            if tab >= 0 && win >= 0
                execute 'tabnext' (tab + 1)
                execute win . 'wincmd w'
                return s:jumpCmd(a:cmd)
            endif
        endif
    endif

    let epath = escape(path, ' ')
    let splitcmd = match(mode, 'split$')

    if count(mode, 'newtab')
        execute 'tabedit' epath
    elseif splitcmd >= 0
        execute mode[splitcmd] epath
    else
        execute 'edit' epath
    endif

    return s:jumpCmd(a:cmd)
endfunction

function! qgrep#utils#gotoFile(path, mode, cmd)
    try
        call s:gotoFile(a:path, a:mode, a:cmd)
    catch
        echohl ErrorMsg
        echo v:exception
    endtry
endfunction
