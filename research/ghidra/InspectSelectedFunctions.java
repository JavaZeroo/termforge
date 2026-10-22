// Local-only decompilation of explicitly selected function entries in a saved project.
// @category TermForge
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import com.google.gson.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;

public class InspectSelectedFunctions extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("Output directory and function entries required");
        Path output = Paths.get(args[0]);
        Files.createDirectory(output);
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try {
            for (int i = 1; i < args.length; i++) {
                monitor.checkCancelled();
                Function function = getFunctionAt(toAddr(args[i]));
                if (function == null) throw new IllegalArgumentException("No function at " + args[i]);
                JsonObject result = new JsonObject();
                result.addProperty("entry", function.getEntryPoint().toString());
                result.addProperty("name", function.getName(true));
                result.addProperty("target_sha256", currentProgram.getExecutableSHA256());
                StringBuilder assembly = new StringBuilder();
                InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
                while (instructions.hasNext()) {
                    Instruction instruction = instructions.next();
                    assembly.append(instruction.getAddress()).append(" ").append(instruction).append("\n");
                }
                Files.writeString(output.resolve(args[i] + ".asm"), assembly.toString(), StandardCharsets.UTF_8);
                DecompileResults decompiled = decompiler.decompileFunction(function, 60, monitor);
                result.addProperty("decompilation_completed", decompiled.decompileCompleted());
                result.addProperty("error", decompiled.getErrorMessage());
                if (decompiled.decompileCompleted())
                    Files.writeString(output.resolve(args[i] + ".c"), decompiled.getDecompiledFunction().getC(), StandardCharsets.UTF_8);
                JsonArray callees = new JsonArray();
                for (Function callee : function.getCalledFunctions(monitor)) {
                    JsonObject row = new JsonObject();
                    row.addProperty("entry", callee.getEntryPoint().toString());
                    row.addProperty("name", callee.getName(true)); callees.add(row);
                }
                result.add("resolved_callees", callees);
                JsonArray incoming = new JsonArray();
                ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(function.getEntryPoint());
                while (refs.hasNext()) {
                    Reference ref = refs.next();
                    JsonObject row = new JsonObject();
                    row.addProperty("from", ref.getFromAddress().toString());
                    row.addProperty("type", ref.getReferenceType().toString()); incoming.add(row);
                }
                result.add("entry_references", incoming);
                Files.writeString(output.resolve(args[i] + ".json"), new GsonBuilder().setPrettyPrinting().create().toJson(result) + "\n", StandardCharsets.UTF_8);
                println("TERMFORGE_FUNCTION_COMPLETE " + args[i]);
            }
        } finally { decompiler.dispose(); }
    }
}
