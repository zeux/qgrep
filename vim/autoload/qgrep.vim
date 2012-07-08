" Global options
let s:globalopts = {
    \ 'guicursor': 'a:blinkon0',
    \ 'hlsearch': 0,
    \ 'imdisable': 1,
    \ 'mouse': 'n',
    \ 'mousef': 0,
    \ 'showcmd': 0,
    \ 'timeout': 1,
    \ 'timeoutlen': 0,
    \ 'updatetime': 4000,
    \ }

" Key mappings
let s:keymap = {
    \ 's:onDeleteChar(%s, -1)':     ['<BS>', '<C-]>'],
    \ 's:onDeleteChar(%s, 0)':      ['<Del>'],
    \ 's:onMoveLine(%s, "j")':      ['<C-j>', '<Down>'],
    \ 's:onMoveLine(%s, "k")':      ['<C-k>', '<Up>'],
    \ 's:onMoveLine(%s, "gg")':     ['<C-Home>', '<C-kHome>'],
    \ 's:onMoveLine(%s, "G")':      ['<C-End>', '<C-kEnd>'],
    \ 's:onMoveLine(%s, "pk")':     ['<PageUp>', '<kPageUp>'],
    \ 's:onMoveLine(%s, "pj")':     ['<PageDown>', '<kPageDown>'],
    \ 's:onMoveCursor(%s, -1)':     ['<C-h>', '<Left>', '<C-^>'],
    \ 's:onMoveCursor(%s, +1)':     ['<C-l>', '<Right>'],
    \ 's:onMoveCursor(%s, -1000)':  ['<Home>', '<kHome>'],
    \ 's:onMoveCursor(%s, +1000)':  ['<End>', '<kEnd>'],
    \ 'qgrep#close()':              ['<Esc>', '<C-c>'],
    \ }

function! s:state()
    return s:state
endfunction

function! s:modecall(state, name, args)
    return call(printf('qgrep#%s#%s', a:state.mode, a:name), [a:state] + a:args)
endfunction

function! s:echoHighlight(group, text)
    execute 'echohl' a:group
    echon a:text
    echohl None
endfunction

function! s:renderPrompt(state)
    let state = a:state
    let text = state.input
    let cursor = state.cursor

    redraw
    call s:echoHighlight('QgrepPromptPrompt', '>>> ')
    call s:echoHighlight('QgrepPromptText', strpart(text, 0, cursor))
    call s:echoHighlight('QgrepPromptCursor', strpart(text, cursor, 1))
    call s:echoHighlight('QgrepPromptText', strpart(text, cursor + 1))

    if cursor >= len(text)
        call s:echoHighlight('QgrepPromptCursor', '_')
    endif
endfunction

function! s:renderResults(lines, maxheight)
    let height = min([len(a:lines), a:maxheight])
    setlocal modifiable
    silent! execute '%d'
    silent! execute 'resize' height
    call setline(1, map(copy(a:lines), '"  ".v:val'))
    setlocal nomodifiable
endfunction

function! s:renderStatus(state, matches, time)
    let res = []

    call add(res, 'qgrep '.a:state.mode)
    call add(res, a:state.config.project)

    if a:matches < a:state.config.limit
        call add(res, printf("%d matches", a:matches))
    else
        call add(res, printf("%d+ matches", a:matches))
    endif

    let groups = ['QgrepStatusOdd', 'QgrepStatusEven']

    let &l:statusline = join(map(copy(res), '"%#" . groups[v:key % len(groups)] . "# " . v:val . " %*"'), '') . printf('%%=%.f ms', a:time)
endfunction

function! s:diffms(start, end)
    return str2float(reltimestr(reltime(a:start, a:end))) * 1000
endfunction

function! s:updateResults(state)
    let state = a:state
    let pattern = s:modecall(state, 'parseInput', [state.input])

    if has_key(state, 'lastpattern') && state.lastpattern ==# pattern
        return
    end

    let start = reltime()
    let results = s:modecall(state, 'getResults', [pattern])
    let lines = s:modecall(state, 'formatResults', [results])
    call s:renderResults(lines, state.config.maxheight)
    call cursor(state.line, 1)
    let end = reltime()

    let state.results = results
    let state.lastpattern = pattern

    call s:renderStatus(state, len(results), s:diffms(start, end))
endfunction

function! s:onInputChanged(state)
    if !has('autocmd') || a:state.config.lazyupdate == 0
        call s:updateResults(a:state)
    endif
    call s:renderPrompt(a:state)
endfunction

function! s:onPromptChanged(state)
    call s:renderPrompt(a:state)
endfunction

function! s:onInsertChar(state, char)
    let state = a:state
    let state.input = strpart(state.input, 0, state.cursor) . a:char . strpart(state.input, state.cursor)
    let state.cursor += len(a:char)
    call s:onInputChanged(state)
endfunction

function! s:onDeleteChar(state, offset)
    let state = a:state
    let state.input = strpart(state.input, 0, state.cursor + a:offset) . strpart(state.input, state.cursor + a:offset + 1)
    if state.cursor > 0 && a:offset < 0
        let state.cursor -= 1
    endif
    call s:onInputChanged(state)
endfunction

function! s:onMoveCursor(state, diff)
    let state = a:state
    let state.cursor += a:diff
    let state.cursor = max([0, min([state.cursor, len(state.input)])])
    call s:onPromptChanged(state)
endfunction

function! s:onMoveLine(state, type)
    let state = a:state
    let motion = (a:type[0] == 'p') ? winheight(0) . a:type[1:] : a:type
    execute 'keepjumps' 'normal!' motion '0'
    let state.line = line('.')
    call s:onPromptChanged(state)
endfunction

function! s:initSyntax()
    syntax clear

    highlight default link QgrepPromptPrompt Comment
    highlight default link QgrepPromptText Normal
    highlight default link QgrepPromptCursor Constant

    highlight default link QgrepStatusOdd LineNr
    highlight default link QgrepStatusEven None

    if has('conceal')
        syntax region QgrepMatch
            \ matchgroup=QgrepMatchBeg start=/\%o33\[.\{-}m/
            \ matchgroup=QgrepMatchEnd end=/\%o33\[0m/
            \ concealends

        highlight default link QgrepMatch Identifier

        setlocal concealcursor=n
        setlocal conceallevel=2
    endif
endfunction

function! s:initOptions(state)
    " Global options
    let a:state.globalopts = {}

    for [k, v] in items(s:globalopts)
        if exists('+'.k)
            let a:state.globalopts[k] = eval('&'.k)
            execute 'let &'.k.'='.string(v)
        endif
    endfor

    " Local options
    setlocal bufhidden=unload
    setlocal nobuflisted
    setlocal buftype=nofile
    setlocal colorcolumn=0
    setlocal nocursorcolumn
    setlocal cursorline
    setlocal foldcolumn=0
    setlocal nofoldenable
    setlocal nolist
    setlocal nomodifiable
    setlocal number
    setlocal numberwidth=4
    setlocal norelativenumber
    setlocal noreadonly
    setlocal nospell
    setlocal noswapfile
    setlocal winfixheight
    setlocal nowrap

    " Custom options
    if a:state.config.lazyupdate
        let &updatetime = (a:state.config.lazyupdate > 1) ? a:state.config.lazyupdate : 250
    endif
endfunction

function! s:initKeys(state, stateexpr)
    let charcmd = 'nnoremap <buffer> <silent> %s :call <SID>onInsertChar(%s, "%s")<CR>'

    " fix arrow keys
    if (has('termresponse') && v:termresponse =~ "\<Esc>") || &term =~? '\vxterm|<k?vt|gnome|screen|linux|ansi'
        for mapping in ['\A <Up>', '\B <Down>', '\C <Right>', '\D <Left>']
            execute 'nnoremap <buffer> <silent> <Esc>['.mapping
        endfor
    endif

	" normal keys
	for ch in range(32, 126)
		execute printf(charcmd, printf('<char-%d>', ch), a:stateexpr, escape(nr2char(ch), '"|\'))
	endfor

    " keypad numeric keys
	for ch in range(0, 9)
		execute printf(charcmd, printf('<k%d>', ch), a:stateexpr, ch)
	endfo

    " keypad non-numeric keys
    let kprange = { 'Plus': '+', 'Minus': '-', 'Divide': '/', 'Multiply': '*', 'Point': '.' }

	for [key, ch] in items(kprange)
		execute printf(charcmd, printf('<k%s>', key), a:stateexpr, ch)
	endfo

    " special keys
    for keymap in [s:keymap, a:state.config.keymap]
        for [expr, keys] in items(keymap)
            let expr = stridx(expr, '%s') < 0 ? expr : printf(expr, a:stateexpr)
            let expr = expr[0:1] == 's:' ? '<SID>'.expr[2:] : expr
            for key in keys
                execute 'nnoremap <buffer> <silent>' key ':call' expr '<CR>'
            endfor
        endfor
    endfor
endfunction

function! s:iscmdwin()
	let v:errmsg = ""
	silent! noautocmd wincmd p
	silent! noautocmd wincmd p
	return v:errmsg =~ '^E11:'
endfunction

function! s:open(args)
    if exists('s:state') || s:iscmdwin()
        return
    endif

    let mode = empty(a:args) ? g:qgrep.mode : a:args[0]
    let state = {'cursor': 0, 'input': '', 'line': 0, 'results': [], 'mode': mode, 'config': {}}
    let config = items(g:qgrep) + (has_key(g:qgrep, mode) ? items(g:qgrep[mode]) : [])

    for p in config
        let state.config[p[0]] = p[1]
    endfor

	let state.winrestore = [winrestcmd(), &lines, winnr('$')]

    let s:state = state

	silent! keepalt botright 1new Qgrep
    abclear <buffer>

    call s:initOptions(state)
    if qgrep#utils#syntax()
        call s:initSyntax()
    endif
    call s:initKeys(state, '<SID>state()')

    call s:modecall(state, 'init', [])

    call s:update(state)
endfunction

function! s:close()
    if exists('s:state')
        for [k, v] in items(s:state.globalopts)
            silent! execute 'let &'.k.'='.string(v)
        endfor

        bunload!

        if s:state.winrestore[1] >= &lines && s:state.winrestore[2] == winnr('$')
            execute s:state.winrestore[0]
        endif

        echo
        unlet! s:state
    endif
endfunction

function! s:update(state)
    call s:updateResults(a:state)
    call s:renderPrompt(a:state)
endfunction

function! qgrep#open(...)
    noautocmd call s:open(a:000)
endfunction

function! qgrep#close()
    noautocmd call s:close()
    wincmd p
endfunction

function! qgrep#update()
    if exists('s:state')
        unlet! s:state.lastpattern
        call s:update(s:state)
    endif
endfunction

function! qgrep#acceptSelection(...)
    let state = s:state
    let line = line('.') - 1

    if line >= 0 && line < len(state.results)
        call qgrep#close()
        call s:modecall(state, 'acceptResult', [state.input, state.results[line]] + a:000)
    endif
endfunction

function! qgrep#execute(args)
    let path = g:qgrep.qgrep

    try
        if path[0:7] == 'libcall:'
            let args = join(a:args, "\n")
            let results = libcall(path[8:], 'entryPointVim', args)
        else
            let args = map(copy(a:args), 'shellescape(v:val)')
            let results = system(path . ' ' . join(args, ' '))
        endif

        return split(results, "\n")
    catch
        return []
    endtry
endfunction

function! qgrep#selectProject(...)
    if a:0
        let g:qgrep.project = a:1
        if exists('s:state')
            let s:state.config.project = a:1
            call qgrep#update()
        endif
        if exists('s:grepprg')
            call call('qgrep#replaceGrep', s:grepprg)
        endif
        return
    endif

    let projects = qgrep#execute(['projects'])

    if has('menu') && has('gui_running')
        try
            nunmenu ]Qgrep
        catch
        endtry

        execute 'nnoremenu' ']Qgrep.Select\ project:' '<Nop>'

        for i in range(0, len(projects))
            let proj = i == 0 ? '*' : projects[i - 1]
            let pref = i < 10 ? '&'.i : '\ '
            execute 'nnoremenu' '<silent>' ']Qgrep.'.pref.'\ '.proj ':call qgrep#selectProject("'.proj.'")<CR>'
        endfor
        popup ]Qgrep
    else
        let lines = copy(projects)
        call map(lines, 'printf("%d. %s", v:key + 1, v:val)')
        call insert(lines, 'Select project (*):')

        let choice = inputlist(lines)
        if choice >= 0 && choice <= len(projects)
            call qgrep#selectProject(choice == 0 ? '*' : projects[choice - 1])
        endif
    endif
endfunction

function! qgrep#replaceGrep(...)
    let s:grepprg = a:000
    let opts = empty(s:grepprg) ? ['<project>'] : s:grepprg
    let args = map(copy(opts), 'shellescape(v:val == "<project>" ? g:qgrep.project : v:val)')
    let path = g:qgrep.qgrep
    let path = path[0:7] == 'libcall:' ? path[8:] : path
    let &grepprg = path . ' search ' . join(args, ' ')
endfunction

if has('autocmd')
	augroup QgrepAug
		autocmd!
		autocmd BufLeave Qgrep call s:close()
        autocmd CursorHold Qgrep if s:state.config.lazyupdate | call s:update(s:state) | endif
	augroup END
endif
