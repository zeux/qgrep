function! qgrep#files#init(state)
endfunction

function! qgrep#files#parseInput(state, input)
    return qgrep#utils#splitex(a:input)[0]
endfunction

function! qgrep#files#getResults(state, pattern)
    return qgrep#execute(['files', a:state.config.project, a:state.config.searchtype, qgrep#utils#syntax() && has('conceal') ? 'H' : '', 'L'.a:state.config.limit, a:pattern])
endfunction

function! qgrep#files#formatResults(state, results)
    return a:results
endfunction

function! qgrep#files#acceptResult(state, input, result, ...)
    let path = substitute(a:result, '\%o33\[.\{-}m', '', 'g')
    let cmd = qgrep#utils#splitex(a:input)[1]

    call qgrep#utils#gotoFile(path, a:0 ? a:1 : a:state.config.switchbuf, cmd)
endfunction
