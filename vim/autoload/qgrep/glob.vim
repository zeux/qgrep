let s:cache = {}

function! qgrep#glob#init(state)
    call qgrep#utils#syntax('Path')

    let dir = getcwd()
    if has_key(s:cache, dir)
        let files = s:cache[dir]
    else
        let s:cache[dir] = map(filter(split(globpath(dir, '**'), "\n"), '!isdirectory(v:val)'), 'v:val[len(dir):]')
        let files = s:cache[dir]
    endif

    let a:state.dir = dir
    let a:state.files = files
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
    let cmd = qgrep#utils#splitex(a:input)[1]
    call qgrep#utils#gotoFile(a:state.dir . a:result, a:0 ? a:1 : a:state.config.switchbuf, cmd)
endfunction
