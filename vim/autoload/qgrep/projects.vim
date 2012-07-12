function! qgrep#projects#init(state)
endfunction

function! qgrep#projects#getStatus(state)
    return [a:state.config.project]
endfunction

function! qgrep#projects#parseInput(state, input)
    return a:input
endfunction

function! qgrep#projects#getResults(state, pattern)
    return qgrep#filter(a:state, a:pattern, qgrep#execute(['projects']))
endfunction

function! qgrep#projects#formatResults(state, results)
    return a:results
endfunction

function! qgrep#projects#acceptResult(state, input, result, ...)
    let result = substitute(a:result, '\%o33\[.\{-}m', '', 'g')
    call qgrep#selectProject(result)
endfunction
