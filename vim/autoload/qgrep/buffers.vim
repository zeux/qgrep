function! qgrep#buffers#init(state)
    call qgrep#utils#syntax('Path')
endfunction

function! qgrep#buffers#getStatus(state)
    return ['']
endfunction

function! qgrep#buffers#parseInput(state, input)
    return qgrep#utils#splitex(a:input)[0]
endfunction

function! qgrep#buffers#getResults(state, pattern)
    let ids = filter(range(1, bufnr('$')), 'empty(getbufvar(v:val, "&bt")) && getbufvar(v:val, "&bl") && len(bufname(v:val))')
    let buffers = map(ids, 'fnamemodify(bufname(v:val), ":.")')
    return qgrep#filter(a:state, a:pattern, buffers)
endfunction

function! qgrep#buffers#formatResults(state, results)
    return a:results
endfunction

function! qgrep#buffers#acceptResult(state, input, result, ...)
    let cmd = qgrep#utils#splitex(a:input)[1]
    call qgrep#utils#gotoFile(a:result, a:0 ? a:1 : a:state.config.switchbuf, cmd)
endfunction
