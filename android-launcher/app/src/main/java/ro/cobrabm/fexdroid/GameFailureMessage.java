package ro.cobrabm.fexdroid;

import java.util.Locale;

/** Translate native faults into gameplay-friendly guidance. No raw logs in UI. */
final class GameFailureMessage {
    final String code,title,description,action;
    private GameFailureMessage(String code,String title,String description,String action) {
        this.code=code;this.title=title;this.description=description;this.action=action;
    }
    static GameFailureMessage classify(String reason,String log) {
        String source=((reason==null?"":reason)+"\n"+(log==null?"":log))
            .toLowerCase(Locale.ROOT);
        if(source.contains("device lost during submit") ||
            source.contains("vk_error_device_lost") ||
            source.contains("vk_error_device_lost")) {
            return new GameFailureMessage("TLOU-GPU-01","O JOGO FOI INTERROMPIDO",
                "O dispositivo gráfico parou de responder. A sessão não pode continuar com segurança.",
                "Tente reiniciar o jogo. Caso se repita, selecione outro driver Vulkan nos ajustes.");
        }
        if(source.contains("job system has been inactive") ||
           source.contains("jobarray waiting on counter")) {
            return new GameFailureMessage("TLOU-JOB-02","O JOGO PAROU DE RESPONDER",
                "Uma tarefa interna do jogo ficou bloqueada e a sessão foi encerrada.",
                "Reinicie o jogo e tente carregar o último save. Os registros da falha foram preservados.");
        }
        if(source.contains("libvulkan_freedreno.so") &&
           (source.contains("host fault") || source.contains("signal 11"))) {
            return new GameFailureMessage("TLOU-DRV-03","FALHA NO DRIVER GRÁFICO",
                "O driver Vulkan encontrou uma falha durante a execução.",
                "Reinicie o jogo. Se voltar a ocorrer, escolha outra versão do Turnip nos ajustes.");
        }
        if(source.contains("imagem preparada ausente")||source.contains("boot-linked.bin")) {
            return new GameFailureMessage("TLOU-DATA-04","ARQUIVOS DO JOGO INDISPONÍVEIS",
                "Não foi possível encontrar os arquivos preparados necessários à inicialização.",
                "Volte ao launcher e selecione a pasta do jogo para prepará-lo novamente.");
        }
        return new GameFailureMessage("TLOU-RUN-00","NÃO FOI POSSÍVEL CONTINUAR",
            "O jogo encontrou um problema e precisou encerrar esta sessão.",
            "Volte ao launcher e tente novamente. O diagnóstico técnico foi salvo localmente.");
    }
}
