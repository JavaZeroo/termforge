// Local-only search for Qt Ctrl+C key encodings and generic control-key masks.
// @category TermForge
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import com.google.gson.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

public class FindKeyConstants extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("Expected new export directory");
        Path output = Paths.get(args[0]);
        Files.createDirectory(output);
        Set<Long> wanted = Set.of(0x04000043L, 0x04000003L, 0x04000000L, 0x43L, 0x1fL);
        Set<String> seen = new HashSet<>();
        JsonArray matches = new JsonArray();
        InstructionIterator instructions = currentProgram.getListing().getInstructions(true);
        while (instructions.hasNext()) {
            monitor.checkCancelled();
            Instruction instruction = instructions.next();
            Function owner = currentProgram.getFunctionManager().getFunctionContaining(instruction.getAddress());
            if (owner == null) continue;
            for (int operand = 0; operand < instruction.getNumOperands(); operand++) {
                for (Object object : instruction.getOpObjects(operand)) {
                    if (!(object instanceof Scalar)) continue;
                    long value = ((Scalar) object).getUnsignedValue();
                    if (!wanted.contains(value)) continue;
                    String key = owner.getEntryPoint() + ":" + Long.toHexString(value);
                    if (!seen.add(key)) continue;
                    JsonObject row = new JsonObject();
                    row.addProperty("function_entry", owner.getEntryPoint().toString());
                    row.addProperty("function_name", owner.getName(true));
                    row.addProperty("constant", "0x" + Long.toHexString(value));
                    row.addProperty("example_instruction", instruction.getAddress() + " " + instruction);
                    matches.add(row);
                }
            }
        }
        JsonObject summary = new JsonObject();
        summary.addProperty("kind", "static_key_constant_search");
        summary.addProperty("target_sha256", currentProgram.getExecutableSHA256());
        summary.addProperty("target_executed", false);
        summary.addProperty("match_count", matches.size());
        summary.addProperty("limitations", "Instruction scalars only; no completeness or semantic/runtime claim.");
        Gson gson = new GsonBuilder().setPrettyPrinting().create();
        Files.writeString(output.resolve("summary.json"), gson.toJson(summary) + "\n", StandardCharsets.UTF_8);
        Files.writeString(output.resolve("matches.json"), gson.toJson(matches) + "\n", StandardCharsets.UTF_8);
        println("TERMFORGE_KEY_CONSTANTS_COMPLETE " + matches.size());
    }
}
