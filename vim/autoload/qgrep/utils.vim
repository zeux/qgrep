function! qgrep#utils#syntax()
	return has('syntax') && exists('g:syntax_on')
endfunction

function! qgrep#utils#splitex(input)
    let pos = stridx(a:input, ':')
    let pos = pos < 0 ? len(a:input) : pos
    return [strpart(a:input, 0, pos), strpart(a:input, pos)]
endfunction
