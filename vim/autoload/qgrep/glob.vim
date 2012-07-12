let s:cache = {}

function! qgrep#glob#init(state)
    let dir = getcwd()
    if has_key(s:cache, dir)
        let files = s:cache[dir]
    else
        let s:cache[dir] = map(filter(split(globpath(dir, '**'), "\n"), '!isdirectory(v:val)'), 'v:val[len(dir):]')
        let files = s:cache[dir]
    endif

    let a:state.dir = dir
    let a:state.files = files

    if qgrep#utils#syntax()
        syntax match QgrepGlobPath "\(\%o33\@!.\)\+\([/\\]\|\(\%o33.\+/\)\@=\)" oneline

        highlight default link QgrepGlobPath SpecialComment
    endif
endfunction

function! qgrep#glob#getStatus(state)
    return [a:state.dir]
endfunction

function! qgrep#glob#parseInput(state, input)
    return qgrep#utils#splitex(a:input)[0]
endfunction

function! qgrep#glob#getResults(state, pattern)
    return qgrep#filter(a:state, a:pattern, a:state.files)
endfunction

function! qgrep#glob#formatResults(state, results)
    return a:results
endfunction

function! qgrep#glob#acceptResult(state, input, result, ...)
    let result = a:state.dir . substitute(a:result, '\%o33\[.\{-}m', '', 'g')
    let cmd = qgrep#utils#splitex(result)[1]
    call qgrep#utils#gotoFile(result, a:0 ? a:1 : a:state.config.switchbuf, cmd)
endfunction
