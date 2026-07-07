local M = {}

local function plugin_root()
  local source = debug.getinfo(1, "S").source:sub(2)
  return vim.fn.fnamemodify(source, ":p:h:h:h")
end

local function root_dir(bufnr)
  local name = vim.api.nvim_buf_get_name(bufnr)
  if vim.fs and vim.fs.root then
    return vim.fs.root(name, { ".git", ".jj", "assembler", "disassembler" }) or vim.fn.getcwd()
  end
  local dir = vim.fn.fnamemodify(name, ":p:h")
  while dir and dir ~= "/" do
    if vim.fn.isdirectory(dir .. "/.git") == 1 or vim.fn.isdirectory(dir .. "/.jj") == 1 then
      return dir
    end
    dir = vim.fn.fnamemodify(dir, ":h")
  end
  return vim.fn.getcwd()
end

function M.setup(opts)
  opts = opts or {}
  local python = opts.python or vim.fn.exepath("python3")
  if python == "" then
    python = "python3"
  end
  local server = opts.server or (plugin_root() .. "/server/e16_lsp.py")

  vim.api.nvim_create_autocmd("FileType", {
    pattern = "e16",
    group = vim.api.nvim_create_augroup("e16_lsp", { clear = true }),
    callback = function(args)
      vim.lsp.start({
        name = "e16-lsp",
        cmd = { python, server },
        root_dir = root_dir(args.buf),
        capabilities = vim.lsp.protocol.make_client_capabilities(),
      }, {
        bufnr = args.buf,
      })
    end,
  })
end

return M
