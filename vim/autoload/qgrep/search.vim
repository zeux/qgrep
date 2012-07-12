function! qgrep#search#init(state)
    if qgrep#utils#syntax()
        syntax match QgrepSearchPath "^.\{-}\ze:\d\+:" nextgroup=QgrepSearchLine
        syntax match QgrepSearchLine ":\zs\d\+\ze:"

        highlight default link QgrepSearchPath Directory
        highlight default link QgrepSearchLine LineNr
    endif
endfunction

function! qgrep#search#getStatus(state)
    return [a:state.config.project]
endfunction

function! qgrep#search#parseInput(state, input)
    return a:input
endfunction

function! qgrep#search#getResults(state, pattern)
    return qgrep#execute(['search', a:state.config.project, 'HM', 'L'.a:state.config.limit, a:pattern])
endfunction

function! qgrep#search#formatResults(state, results)
    return a:results
endfunction

function! qgrep#search#acceptResult(state, input, result, ...)
    let res = matchlist(a:result, '^\(.\{-}\):\(\d\+\):')

    if !empty(res)
        call qgrep#utils#gotoFile(res[1], a:0 ? a:1 : a:state.config.switchbuf, ':'.res[2])
    endif
endfunction
