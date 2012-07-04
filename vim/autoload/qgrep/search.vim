function! qgrep#search#init(state)
endfunction

function! qgrep#search#parseInput(state, input)
    return a:input
endfunction

function! qgrep#search#getResults(state, pattern)
    return qgrep#execute(['search', g:Qgrep.project, 'L'.a:state.limit, a:pattern])
endfunction

function! qgrep#search#formatResults(state, results)
    return a:results
endfunction

function! qgrep#search#parseResult(state, input, result)
    let res = matchlist(a:result, '^\(.\{-}\):\(\d\+\):')
    return [res[1], ':'.res[2]]
endfunction
