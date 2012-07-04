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
    let hl = g:Qgrep.highlight

    redraw
    call s:echoHighlight(hl.prompt, '>>> ')
    call s:echoHighlight('Normal', strpart(text, 0, cursor))
    call s:echoHighlight(hl.cursor, strpart(text, cursor, 1))
    call s:echoHighlight('Normal', strpart(text, cursor + 1))

    if cursor >= len(text)
        call s:echoHighlight(hl.cursor, '_')
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
    call add(res, g:Qgrep.project)

    if a:matches < a:state.limit
        call add(res, printf("%d matches", a:matches))
    else
        call add(res, printf("%d+ matches", a:matches))
    endif

    let groups = g:Qgrep.highlight.status

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
    call s:renderResults(lines, g:Qgrep.maxheight)
    call cursor(state.line, 1)
    let end = reltime()

    let state.results = results
    let state.lastpattern = pattern

    call s:renderStatus(state, len(results), s:diffms(start, end))
endfunction

function! s:onInputChanged(state)
    if !has('autocmd') || g:Qgrep.lazyupdate == 0
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
    let state.cursor += 1
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

    if has('conceal')
        syntax region EscapeMatch
            \ matchgroup=EscapeMatchBeg start=/\%o33\[.\{-}m/
            \ matchgroup=EscapeMatchEnd end=/\%o33\[0m/
            \ concealends

        execute 'highlight link EscapeMatch' g:Qgrep.highlight.match

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
    if g:Qgrep.lazyupdate
        let &updatetime = (g:Qgrep.lazyupdate > 1) ? g:Qgrep.lazyupdate : 250
    endif
endfunction

function! s:initKeys(stateexpr)
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
    for keymap in [s:keymap, g:Qgrep.keymap]
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
	return v:errmsg =~ '^E11:'
endfunction

function! s:open(args)
    if exists('s:state') || s:iscmdwin()
        return
    endif

    let state = {}
    let state.cursor = 0
    let state.input = ''
    let state.line = 0
    let state.limit = g:Qgrep.limit
    let state.results = []
    let state.mode = empty(a:args) ? g:Qgrep.mode : a:args[0]

    let s:state = state

	silent! keepalt botright 1new Qgrep
    abclear <buffer>

    call s:initOptions(state)
    if qgrep#utils#syntax()
        call s:initSyntax()
    endif
    call s:initKeys('<SID>state()')

    call s:modecall(state, 'init', [])

    call s:update(state)
endfunction

function! s:close()
    if exists('s:state')
        for [k, v] in items(s:state.globalopts)
            silent! execute 'let &'.k.'='.string(v)
        endfor

        bunload!
        echo
        unlet! s:state
    endif
endfunction

function! s:update(state)
    call s:updateResults(a:state)
    call s:renderPrompt(a:state)
endfunction

function! s:tabpagebufwinnr(idx)
    for i in range(tabpagenr('$'))
        let win = index(tabpagebuflist(i + 1), a:idx)
        if win >= 0
            return [i, win]
        endif
    endfor

    return [-1, -1]
endfunction

function! qgrep#open(...)
    noautocmd call s:open(a:000)
endfunction

function! qgrep#close()
    noautocmd call s:close()
endfunction

function! qgrep#update()
    if exists('s:state')
        unlet! s:state.lastpattern
        call s:update(s:state)
    endif
endfunction

function! s:jumpCmd(cmd)
    if a:cmd != ''
        silent! execute a:cmd
        silent! normal! zvzz
    endif
endfunction

function! qgrep#gotoFile(path, mode, cmd)
    let path = fnamemodify(a:path, ':p')
    let mode = split(a:mode, ',')

    let buf = bufnr('^'.path.'$')

    if (count(mode, 'useopen') || count(mode, 'usetab')) && buf >= 0
        let win = bufwinnr(buf)
        if win >= 0
            execute win . 'wincmd w'
            return s:jumpCmd(a:cmd)
        endif

        if count(mode, 'usetab')
            let [tab, win] = s:tabpagebufwinnr(buf)
            if tab >= 0 && win >= 0
                execute 'tabnext' (tab + 1)
                execute win . 'wincmd w'
                return s:jumpCmd(a:cmd)
            endif
        endif
    endif

    if count(mode, 'newtab')
        execute 'tabedit' path
    elseif count(mode, 'split')
        execute 'split' path
    elseif count(mode, 'vsplit')
        execute 'vsplit' path
    else
        execute 'edit' path
    endif

    return s:jumpCmd(a:cmd)
endfunction

function! qgrep#acceptSelection(mode)
    let state = s:state
    let line = line('.') - 1
    call qgrep#close()

    if line >= 0 && line < len(state.results)
        let res = s:modecall(state, 'parseResult', [state.input, state.results[line]])

        if !empty(res)
            let [path, cmd] = res

            try
                call qgrep#gotoFile(path, a:mode, cmd)
            catch
                echohl ErrorMsg
                echo v:exception
            endtry
        endif
    endif
endfunction

function! qgrep#execute(args)
    let path = g:Qgrep.qgrep

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
        let g:Qgrep.project = a:1
        return
    endif

    let projects = qgrep#execute(['projects'])

    let lines = copy(projects)
    call map(lines, 'printf("%2d. %s", v:key + 1, v:val)')
    call insert(lines, 'Select project (*):')

    let choice = inputlist(lines)
    if choice >= 0 && choice <= len(projects)
        let project = (choice == 0) ? '*' : projects[choice - 1]
        let g:Qgrep.project = project

        call qgrep#update()
    endif
endfunction

if has('autocmd')
	augroup QgrepAug
		autocmd!
		autocmd BufLeave Qgrep call qgrep#close()
        autocmd CursorHold Qgrep if g:Qgrep.lazyupdate | call s:update(s:state) | endif
	augroup END
endif
