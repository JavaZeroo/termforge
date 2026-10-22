// Export a local-only static inventory and bounded references for terminal research.
// No target execution, decompilation output, or database mutation.
// @category TermForge
import ghidra.app.script.GhidraScript;
import ghidra.framework.Application;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.Address;
import com.google.gson.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.Locale;
import java.util.regex.Pattern;

public class ExportTerminalIndex extends GhidraScript {
    private final Gson gson = new GsonBuilder().disableHtmlEscaping().create();
    private final Pattern seeds = Pattern.compile(
        "pty|pipethread|superbuffer|documenttaskrunner|readyread|appendbuffer|" +
        "createpseudoconsole|resizepseudoconsole|closepseudoconsole|" +
        "readfile|writefile|zstd_|qwineventnotifier|qsocketnotifier",
        Pattern.CASE_INSENSITIVE);

    private JsonArray references(Address address, JsonObject row) throws Exception {
        JsonArray result = new JsonArray();
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(address);
        long count = 0;
        while (refs.hasNext()) {
            monitor.checkCancelled();
            Reference ref = refs.next();
            count++;
            if (result.size() >= 256) continue;
            JsonObject item = new JsonObject();
            item.addProperty("from", ref.getFromAddress().toString());
            item.addProperty("type", ref.getReferenceType().toString());
            Function owner = currentProgram.getFunctionManager().getFunctionContaining(ref.getFromAddress());
            if (owner != null) {
                item.addProperty("function_entry", owner.getEntryPoint().toString());
                item.addProperty("function_name", owner.getName(true));
            }
            result.add(item);
        }
        row.addProperty("incoming_reference_count", count);
        row.addProperty("references_truncated", count > 256);
        return result;
    }

    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("Expected new export directory");
        Path output = Paths.get(args[0]);
        Files.createDirectory(output);
        long functions = 0, symbolHits = 0, stringHits = 0;
        try (BufferedWriter all = Files.newBufferedWriter(output.resolve("functions.jsonl"), StandardCharsets.UTF_8);
             BufferedWriter candidates = Files.newBufferedWriter(output.resolve("candidates.jsonl"), StandardCharsets.UTF_8)) {
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext()) {
                monitor.checkCancelled();
                Function f = it.next();
                JsonObject row = new JsonObject();
                row.addProperty("entry", f.getEntryPoint().toString());
                row.addProperty("name", f.getName(true));
                row.addProperty("body_address_count", f.getBody().getNumAddresses());
                row.addProperty("thunk", f.isThunk());
                row.addProperty("external", f.isExternal());
                all.write(gson.toJson(row)); all.newLine(); functions++;
            }
            SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
            while (symbols.hasNext()) {
                monitor.checkCancelled();
                Symbol symbol = symbols.next();
                String name = symbol.getName(true);
                if (!seeds.matcher(name).find()) continue;
                JsonObject row = new JsonObject();
                row.addProperty("kind", "symbol");
                row.addProperty("address", symbol.getAddress().toString());
                row.addProperty("name", name);
                row.addProperty("symbol_type", symbol.getSymbolType().toString());
                row.addProperty("source", symbol.getSource().toString());
                row.add("incoming_references", references(symbol.getAddress(), row));
                candidates.write(gson.toJson(row)); candidates.newLine(); symbolHits++;
            }
            DataIterator data = currentProgram.getListing().getDefinedData(true);
            while (data.hasNext()) {
                monitor.checkCancelled();
                Data d = data.next();
                if (!d.hasStringValue()) continue;
                Object value = d.getValue();
                if (!(value instanceof String) || !seeds.matcher((String)value).find()) continue;
                JsonObject row = new JsonObject();
                row.addProperty("kind", "defined_string");
                row.addProperty("address", d.getAddress().toString());
                row.addProperty("value", (String)value);
                row.add("incoming_references", references(d.getAddress(), row));
                candidates.write(gson.toJson(row)); candidates.newLine(); stringHits++;
            }
        }
        JsonObject summary = new JsonObject();
        summary.addProperty("schema_version", "1");
        summary.addProperty("kind", "ghidra_static_terminal_index");
        summary.addProperty("ghidra_version", Application.getApplicationVersion());
        summary.addProperty("executable_sha256", currentProgram.getExecutableSHA256());
        summary.addProperty("image_base", currentProgram.getImageBase().toString());
        summary.addProperty("language", currentProgram.getLanguageID().toString());
        summary.addProperty("defined_functions", functions);
        summary.addProperty("matched_symbols", symbolHits);
        summary.addProperty("matched_defined_strings", stringHits);
        summary.addProperty("seed_regex", seeds.pattern());
        summary.addProperty("target_executed", false);
        summary.addProperty("limitations", "Static Ghidra-defined inventory only, not runtime or complete source recovery. Incoming references capped at 256 per candidate; unresolved indirect edges and undefined strings may be missing. Names/types may be analyzer inferences. Raw exports are local-only.");
        Files.writeString(output.resolve("summary.json"), new GsonBuilder().setPrettyPrinting().create().toJson(summary) + "\n", StandardCharsets.UTF_8);
        println("TERMFORGE_INDEX_COMPLETE " + functions + " functions, " + symbolHits + " symbols, " + stringHits + " strings");
    }
}
