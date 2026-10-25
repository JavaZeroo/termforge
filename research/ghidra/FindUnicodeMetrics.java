// Local-only bounded inventory of Unicode/font metric paths and direct references.
// @category TermForge
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import com.google.gson.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.regex.Pattern;

public class FindUnicodeMetrics extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("Expected new export directory");
        Path output = Paths.get(args[0]);
        Files.createDirectory(output);
        Pattern seeds = Pattern.compile("qfontmetrics|qrawfont|qtextlayout|qchar|unicode|combining|charwidth|wcwidth", Pattern.CASE_INSENSITIVE);
        Gson gson = new GsonBuilder().disableHtmlEscaping().setPrettyPrinting().create();
        JsonArray matches = new JsonArray();
        int total = 0;
        SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
        while (symbols.hasNext()) {
            monitor.checkCancelled();
            Symbol symbol = symbols.next();
            String name = symbol.getName(true);
            if (!seeds.matcher(name).find()) continue;
            ++total;
            if (matches.size() >= 128) continue;
            JsonObject row = new JsonObject();
            row.addProperty("address", symbol.getAddress().toString());
            row.addProperty("name", name);
            row.addProperty("symbol_type", symbol.getSymbolType().toString());
            row.addProperty("source", symbol.getSource().toString());
            JsonArray references = new JsonArray();
            int count = 0;
            ReferenceIterator incoming = currentProgram.getReferenceManager().getReferencesTo(symbol.getAddress());
            while (incoming.hasNext()) {
                monitor.checkCancelled();
                Reference reference = incoming.next();
                ++count;
                if (references.size() >= 64) continue;
                JsonObject item = new JsonObject();
                item.addProperty("from", reference.getFromAddress().toString());
                item.addProperty("type", reference.getReferenceType().toString());
                Function owner = currentProgram.getFunctionManager().getFunctionContaining(reference.getFromAddress());
                if (owner != null) item.addProperty("function_entry", owner.getEntryPoint().toString());
                references.add(item);
            }
            row.addProperty("incoming_reference_count", count);
            row.addProperty("references_truncated", count > 64);
            row.add("incoming_references", references);
            matches.add(row);
        }
        JsonObject summary = new JsonObject();
        summary.addProperty("kind", "static_unicode_metric_inventory");
        summary.addProperty("target_sha256", currentProgram.getExecutableSHA256());
        summary.addProperty("target_executed", false);
        summary.addProperty("seed_regex", seeds.pattern());
        summary.addProperty("matched_symbols", total);
        summary.addProperty("recorded_symbols", matches.size());
        summary.addProperty("symbol_limit", 128);
        summary.addProperty("reference_limit", 64);
        summary.addProperty("limitations", "Static defined symbols and direct references only; indirect, Qt-internal and runtime behavior is not observed. Bounded results are not proof of absence.");
        Files.writeString(output.resolve("matches.json"), gson.toJson(matches) + "\n", StandardCharsets.UTF_8);
        Files.writeString(output.resolve("summary.json"), gson.toJson(summary) + "\n", StandardCharsets.UTF_8);
        println("TERMFORGE_UNICODE_METRICS_COMPLETE " + total + " symbols");
    }
}
