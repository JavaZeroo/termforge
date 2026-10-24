// Local-only inventory of input/control strings, imports and ConPty vtable entries.
// @category TermForge
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import com.google.gson.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.regex.Pattern;

public class InspectInputPaths extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("Expected new export directory");
        Path output = Paths.get(args[0]);
        Files.createDirectory(output);
        Gson gson = new GsonBuilder().disableHtmlEscaping().setPrettyPrinting().create();
        Pattern pattern = Pattern.compile("ctrl|interrupt|cancel|large output|too much|suspend|terminate|keypress|sendkey|sendtext|control", Pattern.CASE_INSENSITIVE);
        JsonArray matches = new JsonArray();
        SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
        while (symbols.hasNext()) {
            Symbol symbol = symbols.next();
            String name = symbol.getName(true);
            if (!pattern.matcher(name).find()) continue;
            JsonObject row = new JsonObject();
            row.addProperty("kind", "symbol");
            row.addProperty("address", symbol.getAddress().toString());
            row.addProperty("name", name);
            matches.add(row);
        }
        DataIterator data = currentProgram.getListing().getDefinedData(true);
        while (data.hasNext()) {
            Data item = data.next();
            if (!item.hasStringValue() || !(item.getValue() instanceof String)) continue;
            String value = (String) item.getValue();
            if (!pattern.matcher(value).find()) continue;
            JsonObject row = new JsonObject();
            row.addProperty("kind", "defined_string");
            row.addProperty("address", item.getAddress().toString());
            row.addProperty("value", value);
            JsonArray references = new JsonArray();
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(item.getAddress());
            while (refs.hasNext()) {
                Reference ref = refs.next();
                JsonObject reference = new JsonObject();
                reference.addProperty("from", ref.getFromAddress().toString());
                Function owner = currentProgram.getFunctionManager().getFunctionContaining(ref.getFromAddress());
                if (owner != null) reference.addProperty("function_entry", owner.getEntryPoint().toString());
                references.add(reference);
            }
            row.add("references", references);
            matches.add(row);
        }
        JsonArray vtable = new JsonArray();
        Address start = toAddr("140a2f900");
        for (int offset = 0; offset < 0x300; offset += 8) {
            Address pointer = start.add(offset);
            Function function = getFunctionAt(toAddr(currentProgram.getMemory().getLong(pointer, false)));
            if (function == null) continue;
            JsonObject row = new JsonObject();
            row.addProperty("pointer_address", pointer.toString());
            row.addProperty("function_entry", function.getEntryPoint().toString());
            row.addProperty("function_name", function.getName(true));
            vtable.add(row);
        }
        JsonObject summary = new JsonObject();
        summary.addProperty("kind", "static_input_path_inventory");
        summary.addProperty("target_sha256", currentProgram.getExecutableSHA256());
        summary.addProperty("target_executed", false);
        summary.addProperty("match_count", matches.size());
        summary.addProperty("pattern", pattern.pattern());
        summary.addProperty("limitations", "Defined strings, symbols and recognized pointers only; not a complete search or runtime observation.");
        Files.writeString(output.resolve("summary.json"), gson.toJson(summary) + "\n", StandardCharsets.UTF_8);
        Files.writeString(output.resolve("matches.json"), gson.toJson(matches) + "\n", StandardCharsets.UTF_8);
        Files.writeString(output.resolve("pty-vtable.json"), gson.toJson(vtable) + "\n", StandardCharsets.UTF_8);
        println("TERMFORGE_INPUT_INDEX_COMPLETE " + matches.size() + " matches, " + vtable.size() + " pointers");
    }
}
