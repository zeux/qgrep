function! qgrep#files#init(state)
endfunction

function! qgrep#files#parseInput(state, input)
    return qgrep#splitExCmd(a:input)[0]
endfunction

function! qgrep#files#getResults(state, pattern)
    return qgrep#execute(['files', g:Qgrep.project, 'ft', qgrep#syntax() && has('conceal') ? 'H' : '', 'L'.a:state.limit, a:pattern])
endfunction

function! qgrep#files#formatResults(state, results)
    return a:results
endfunction

function! qgrep#files#parseResult(state, input, result)
    return [substitute(a:result, '\%o33\[.\{-}m', '', 'g'), qgrep#splitExCmd(a:input)[1]]
endfunction
